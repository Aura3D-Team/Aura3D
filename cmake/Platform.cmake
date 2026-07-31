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