#include "aura/Renderer/Metal/MtlAura/MtlShaderLibraryManager/MtlShaderLibraryManager.h"

#include <string>

#include <ink/ink.hpp>

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"
#include "aura/Renderer/Metal/MtlAura/EmbeddedMetalLib.h"

namespace aura3d {
namespace mtl {

MtlShaderLibraryManager::MtlShaderLibraryManager(MTL::Device* device)
{
    if (!device)
        throw AuraException("MtlShaderLibraryManager: device is null");

#if AURA_METAL_HAS_EMBEDDED_METALLIB
    loadPrecompiledLibrary(device);
#else
    compileSourceLibrary(device);
#endif

    INK_INFO << "Metal shader library loaded from "
             << (_precompiled ? "embedded metallib" : "embedded MSL source");
}

MtlShaderLibraryManager::~MtlShaderLibraryManager()
{
    _library.reset();
    INK_DEBUG << "MtlShaderLibraryManager destroyed";
}

void MtlShaderLibraryManager::loadPrecompiledLibrary(MTL::Device* device)
{
#if AURA_METAL_HAS_EMBEDDED_METALLIB
    /*
     * DISPATCH_DATA_DESTRUCTOR_DEFAULT makes dispatch copy the bytes, so the
     * dispatch_data_t can be released the moment the library is built --
     * newLibrary() has finished reading it by then either way. The alternative
     * (a no-copy destructor over the static array) would save a memcpy of a few
     * kilobytes at the cost of tying the data object's lifetime to the binary's,
     * which is not worth reasoning about.
     */
    dispatch_data_t libraryData = dispatch_data_create(
        mtl_library_data, mtl_library_data_len, nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);

    if (!libraryData)
        throw AuraException("MtlShaderLibraryManager: failed to wrap the embedded metallib");

    NS::Error* error = nullptr;
    _library = adopt(device->newLibrary(libraryData, &error));

    //! Balanced here rather than left to scope exit: dispatch objects are not
    //! reference-counted by NS::SharedPtr, and this TU is plain C++ (no ARC).
    dispatch_release(libraryData);

    if (!_library) {
        throw AuraException("MtlShaderLibraryManager: the embedded metallib was rejected: "
                            + describeError(error));
    }

    _precompiled = true;
#else
    (void)device;
#endif
}

void MtlShaderLibraryManager::compileSourceLibrary(MTL::Device* device)
{
    const NS::SharedPtr<NS::String> source = makeString(mtl_library_source);

    NS::SharedPtr<MTL::CompileOptions> options = adopt(MTL::CompileOptions::alloc()->init());
    /*
     * Pinned to the same language version scripts/gen_embedded_metallib.sh
     * passes to `xcrun metal` (-std=metal3.0), so a shader that compiles offline
     * cannot behave differently when compiled here. Metal 3.0 is macOS 13 /
     * iOS 16 and up, which is at or below the deployment target the Apple
     * presets set (see CMakePresets.json).
     */
    options->setLanguageVersion(MTL::LanguageVersion3_0);

    NS::Error* error = nullptr;
    _library = adopt(device->newLibrary(source.get(), options.get(), &error));

    if (!_library) {
        throw AuraException("MtlShaderLibraryManager: failed to compile the embedded MSL: "
                            + describeError(error));
    }

    _precompiled = false;
}

NS::SharedPtr<MTL::Function> MtlShaderLibraryManager::newFunction(const char* functionName) const
{
    if (!functionName)
        throw AuraException("MtlShaderLibraryManager: function name is null");

    const NS::SharedPtr<NS::String> name = makeString(functionName);
    NS::SharedPtr<MTL::Function> function = adopt(_library->newFunction(name.get()));

    if (!function) {
        throw AuraException("MtlShaderLibraryManager: the shader library has no function named '"
                            + std::string(functionName)
                            + "' (MtlAuraCore.h and resources/shaders/metal/* disagree; "
                              "re-run scripts/gen_embedded_metallib.sh)");
    }

    return function;
}

} // namespace mtl
} // namespace aura3d
