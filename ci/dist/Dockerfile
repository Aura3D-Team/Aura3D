# Build environment for programs that have to run on other people's Linux machines.
#
# A binary needs at least the glibc it was linked against, so this image is the oldest one we support: Ubuntu 18.04
# (glibc 2.27, the floor for Debian 10, RHEL 8 and Ubuntu 18.04 onwards). GCC comes from the toolchain PPA so C++23
# builds, and its libstdc++ is linked statically (AURA_STATIC_RUNTIME), which is why a newer GCC costs nothing.
# X11, Wayland, ALSA and Vulkan are only needed here as headers: the programs open those libraries at run time.
FROM ubuntu:18.04@sha256:152dc042452c496007f07ca9127571cb9c29697f42acbfad72324b2bb2e43c98

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl git gnupg software-properties-common xz-utils python3 python3-pip python3-setuptools \
        ninja-build pkg-config binutils \
    && add-apt-repository -y ppa:ubuntu-toolchain-r/test \
    && apt-get update && apt-get install -y --no-install-recommends gcc-15 g++-15 \
    && rm -rf /var/lib/apt/lists/*
ENV CC=gcc-15 CXX=g++-15 LOCAL_PREFIX=/usr/local
ENV PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/share/pkgconfig
# Read by ci/dist/build.sh: the highest GLIBC_ version a built binary may need.
ENV AURA_GLIBC_FLOOR=2.27

# Headers only. There is no Wayland, libdecor or glslang here on purpose: 18.04's are too old, so they are built below.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libx11-dev libxext-dev libxi-dev libxrandr-dev libxpresent-dev libxcursor-dev libxfixes-dev libxss-dev \
        libxtst-dev libgl1-mesa-dev libegl1-mesa-dev libxkbcommon-dev libasound2-dev libpulse-dev libudev-dev \
        libdbus-1-dev libvulkan-dev libffi-dev libexpat1-dev libxml2-dev \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 18.04's CMake is 3.10; the stack needs 4.3. Kitware's binary runs on this glibc.
RUN curl -fsSL https://github.com/Kitware/CMake/releases/download/v4.3.3/cmake-4.3.3-linux-x86_64.tar.gz -o /tmp/cmake.tar.gz \
    && echo '927b2368a946c37269c3a66225ab00544e756459cdd0b5d0da438694fb9ff802  /tmp/cmake.tar.gz' | sha256sum -c - \
    && tar -xzf /tmp/cmake.tar.gz --strip-components=1 -C /usr/local \
    && rm /tmp/cmake.tar.gz

# Wayland 1.19+ is what libwma's loader needs (generated headers must call wl_proxy_marshal_flags); 18.04 ships 1.16.
# Meson 0.61 is the last that runs on this Python.
RUN pip3 install --no-cache-dir meson==0.61.5
RUN curl -fsSL https://gitlab.freedesktop.org/wayland/wayland/-/releases/1.22.0/downloads/wayland-1.22.0.tar.xz -o /tmp/wayland.tar.xz \
    && echo '1540af1ea698a471c2d8e9d288332c7e0fd360c8f1d12936ebb7e7cbc2425842  /tmp/wayland.tar.xz' | sha256sum -c - \
    && mkdir /tmp/wayland && tar -xJf /tmp/wayland.tar.xz --strip-components=1 -C /tmp/wayland \
    && meson setup /tmp/wayland/build /tmp/wayland --prefix=/usr/local --libdir=lib --buildtype=release \
        -Ddocumentation=false -Dtests=false -Ddtd_validation=false \
    && ninja -C /tmp/wayland/build install \
    && rm -rf /tmp/wayland /tmp/wayland.tar.xz
RUN curl -fsSL https://gitlab.freedesktop.org/wayland/wayland-protocols/-/releases/1.33/downloads/wayland-protocols-1.33.tar.xz -o /tmp/wp.tar.xz \
    && echo '94f0c50b090d6e61a03f62048467b19abbe851be4e11ae7b36f65f8b98c3963a  /tmp/wp.tar.xz' | sha256sum -c - \
    && mkdir /tmp/wp && tar -xJf /tmp/wp.tar.xz --strip-components=1 -C /tmp/wp \
    && meson setup /tmp/wp/build /tmp/wp --prefix=/usr/local --libdir=lib --buildtype=release -Dtests=false \
    && ninja -C /tmp/wp/build install \
    && rm -rf /tmp/wp /tmp/wp.tar.xz

# Vulkan headers newer than the distro's 1.1 (the engine asks for 1.4). libvulkan-dev above still supplies the
# library file FindVulkan insists on; nothing links it.
RUN git clone --quiet https://github.com/KhronosGroup/Vulkan-Headers /tmp/vulkan-headers \
    && git -C /tmp/vulkan-headers checkout --quiet 7feeb71f59b8c1531ef5f7bb4b36e1aac46a0acb \
    && cmake -S /tmp/vulkan-headers -B /tmp/vulkan-headers/build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --install /tmp/vulkan-headers/build \
    && rm -rf /tmp/vulkan-headers
ENV Vulkan_INCLUDE_DIR=/usr/local/include

# The shaders are compiled to SPIR-V at build time; 18.04 has no glslang package.
RUN git clone --quiet https://github.com/KhronosGroup/glslang /tmp/glslang \
    && git -C /tmp/glslang checkout --quiet 275822a6261ee689aadb1da5f09a0ec2f058685c \
    && (cd /tmp/glslang && python3 update_glslang_sources.py) \
    && cmake -S /tmp/glslang -B /tmp/glslang/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DENABLE_OPT=ON -DBUILD_TESTING=OFF \
    && cmake --build /tmp/glslang/build --parallel && cmake --install /tmp/glslang/build \
    && rm -rf /tmp/glslang

RUN git clone --quiet https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator /tmp/vma \
    && git -C /tmp/vma checkout --quiet 3aa921224c154a0d2c43912bc88e1c42ce1f7607 \
    && install -m 644 /tmp/vma/include/vk_mem_alloc.h /usr/local/include/vk_mem_alloc.h \
    && rm -rf /tmp/vma

# Static, and built here so it uses this glibc: a system SDL3 would not be 3.x on 18.04, and an SDL3 built against a
# newer glibc pulls strlcpy (glibc 2.38) into every program. It opens X11, Wayland and ALSA at run time by itself.
RUN curl -fsSL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz -o /tmp/sdl3.tar.gz \
    && echo '9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3  /tmp/sdl3.tar.gz' | sha256sum -c - \
    && mkdir /tmp/sdl3 && tar -xzf /tmp/sdl3.tar.gz --strip-components=1 -C /tmp/sdl3 \
    && cmake -S /tmp/sdl3 -B /tmp/sdl3/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DSDL_STATIC=ON -DSDL_SHARED=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_TESTS=OFF \
    && cmake --build /tmp/sdl3/build --parallel && cmake --install /tmp/sdl3/build \
    && rm -rf /tmp/sdl3 /tmp/sdl3.tar.gz

# libink's JSON: the distro package is older than the 3.12 it asks for.
RUN curl -fsSL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz -o /tmp/json.tar.xz \
    && echo '42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa  /tmp/json.tar.xz' | sha256sum -c - \
    && mkdir /tmp/json && tar -xJf /tmp/json.tar.xz --strip-components=1 -C /tmp/json \
    && cmake -S /tmp/json -B /tmp/json/build -G Ninja -DJSON_BuildTests=OFF -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --install /tmp/json/build \
    && rm -rf /tmp/json /tmp/json.tar.xz

# The distro's glm ships a CMake config that CMake 4 refuses.
RUN curl -fsSL https://github.com/g-truc/glm/archive/refs/tags/1.0.3.tar.gz -o /tmp/glm.tar.gz \
    && echo '6775e47231a446fd086d660ecc18bcd076531cfedd912fbd66e576b118607001  /tmp/glm.tar.gz' | sha256sum -c - \
    && mkdir /tmp/glm && tar -xzf /tmp/glm.tar.gz --strip-components=1 -C /tmp/glm \
    && cmake -S /tmp/glm -B /tmp/glm/build -G Ninja -DGLM_BUILD_TESTS=OFF -DGLM_BUILD_LIBRARY=OFF -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --install /tmp/glm/build \
    && rm -rf /tmp/glm /tmp/glm.tar.gz

WORKDIR /workspace
