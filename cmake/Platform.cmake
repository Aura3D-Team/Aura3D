if(EMSCRIPTEN)
    set(AURA_ENABLE_VULKAN OFF CACHE BOOL "" FORCE)
    set(AURA_ENABLE_OPENGL ON CACHE BOOL "" FORCE)
    set(AURA_ENABLE_CPU OFF CACHE BOOL "" FORCE)

    set(AURA_NATIVE_OPTIMIZE OFF CACHE BOOL "" FORCE)
    set(AURA_ENABLE_LTO OFF CACHE BOOL "" FORCE)
endif()

if(ANDROID)
    set(AURA_ENABLE_VULKAN ON CACHE BOOL "" FORCE)
    set(AURA_ENABLE_OPENGL OFF CACHE BOOL "" FORCE)
    set(AURA_ENABLE_CPU OFF CACHE BOOL "" FORCE)

    set(AURA_BUILD_SANDBOX OFF CACHE BOOL "" FORCE)

    # Aura3D is a static library that gets merged into the app's own
    # libmain.so at final link (see android/app/src/main/cpp/CMakeLists.txt).
    # LTO'd across that static-lib -> shared-object boundary, exceptions
    # thrown from inlined code can lose their unwind tables: the throw still
    # runs, but the unwinder finds no landing pad and calls std::terminate()
    # even though a matching catch exists in the source (observed on-device:
    # AuraException thrown from VkSwapChainManager, with a catch a few
    # frames up in VulkanRenderer::beginFrame(), still aborted the process).
    set(AURA_ENABLE_LTO OFF CACHE BOOL "" FORCE)
endif()

if(WIN32 AND NOT EMSCRIPTEN)
    message(STATUS "[Aura3D] Target platform: Windows  Compiler=${CMAKE_CXX_COMPILER_ID}")

    # <windows.h>'s min/max macros collide with std::min/std::max (Camera,
    # VkAura's extent-clamping, ...) -- both ink and wma already keep this off
    # their own targets, but Aura3D pulls in <windows.h> too via glad.c's WGL
    # loader path when AURA_ENABLE_OPENGL is on, so the same guard applies
    # here. WIN32_LEAN_AND_MEAN additionally keeps the winsock/GDI surface
    # (unused by this engine) out of the build. Applies to every target in
    # the tree, including consumers that pull in <windows.h> themselves.
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)

    if(MSVC)
        # /EHsc: standard C++ exception unwinding (off by default under
        # cl.exe; AuraException and VK_RESULT_CHECK -- plus ink and wma
        # underneath -- all throw). /utf-8: source and execution charset,
        # matching GCC/Clang defaults.
        # /Zc:__cplusplus: cl.exe reports __cplusplus as 199711L regardless
        # of the active /std: flag unless this is set, which trips ink's
        # `#if __cplusplus < 202100L` C++23 guard even when
        # CMAKE_CXX_STANDARD 23 has correctly selected -std:c++latest.
        add_compile_options(/EHsc /utf-8 /Zc:__cplusplus)
    endif()
endif()
