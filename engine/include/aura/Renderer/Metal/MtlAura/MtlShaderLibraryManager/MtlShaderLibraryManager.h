#ifndef MTLSHADERLIBRARYMANAGER_H
#define MTLSHADERLIBRARYMANAGER_H

#pragma once

#include "aura/Renderer/Metal/MtlAura/MtlAuraCore.h"

namespace aura3d {
namespace mtl {

/**
 * @class MtlShaderLibraryManager
 *
 * @brief Owns the MTLLibrary holding every built-in shader, and vends the
 *        MTLFunctions pipelines are built from.
 *
 * The Metal counterpart of ShaderManager + ShaderSpirvExtractor: it boots from
 * shaders compiled into the binary, so a build with no shader files beside it
 * still renders -- the same guarantee EmbeddedSpirv.h gives the Vulkan backend.
 *
 * Two embedded forms exist, and which one is in play is decided at compile time
 * by AURA_METAL_HAS_EMBEDDED_METALLIB in the generated EmbeddedMetalLib.h:
 *
 *  - A precompiled `.metallib`, loaded straight into an MTLLibrary. Preferred,
 *    and what a release build should ship: no compiler runs at startup.
 *  - The MSL source, compiled at construction time. This is what a header
 *    regenerated off an Apple host carries, since only Xcode's Metal toolchain
 *    can produce the compiled form (see scripts/gen_embedded_metallib.sh).
 *    Costs one compile of ~150 lines of MSL during initialize().
 *
 * Both paths yield the same four entry points, so nothing downstream can tell
 * them apart.
 */
class MtlShaderLibraryManager {
public:
    /**
     * @brief Builds the built-in library on @p device.
     *
     * @throws AuraException if @p device is null, or if the embedded library
     *         cannot be loaded/compiled -- which for the source path means the
     *         MSL itself is broken, so the compiler's diagnostic is included in
     *         the message rather than summarised.
     */
    explicit MtlShaderLibraryManager(MTL::Device* device);

    ~MtlShaderLibraryManager();

    MtlShaderLibraryManager(const MtlShaderLibraryManager&) = delete;
    MtlShaderLibraryManager& operator=(const MtlShaderLibraryManager&) = delete;

    /**
     * @brief Looks up one shader entry point by its MSL function name.
     *
     * @param[in] functionName One of MtlAuraCore.h's kVertexFunction3D,
     *        kFragmentFunction3D, kVertexFunction2D, kFragmentFunction2D.
     * @return An owning handle to the function.
     * @throws AuraException if the library holds no function by that name,
     *         which means the MSL and the C++ constants have drifted apart.
     */
    [[nodiscard]] NS::SharedPtr<MTL::Function> newFunction(const char* functionName) const;

    /**
     * @brief Whether the library came from a precompiled .metallib rather than
     *        from MSL compiled at startup.
     *
     * Reported once at initialize() time, so a slow start on a machine whose
     * EmbeddedMetalLib.h was regenerated off-Apple is self-explanatory in the log.
     */
    [[nodiscard]] bool isPrecompiled() const noexcept { return _precompiled; }

private:
    //! Loads the embedded .metallib bytes. Only compiled in when the generated
    //! header actually carries them.
    void loadPrecompiledLibrary(MTL::Device* device);

    //! Compiles the embedded MSL source.
    void compileSourceLibrary(MTL::Device* device);

    NS::SharedPtr<MTL::Library> _library;
    bool _precompiled = false;
};

} // namespace mtl
} // namespace aura3d

#endif // MTLSHADERLIBRARYMANAGER_H
