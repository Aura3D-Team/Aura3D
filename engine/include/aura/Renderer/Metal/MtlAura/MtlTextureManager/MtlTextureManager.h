#ifndef MTLTEXTUREMANAGER_H
#define MTLTEXTUREMANAGER_H

#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "aura/Renderer/Metal/MtlAura/MtlAuraCore.h"
#include "aura/Renderer/RenderHandles.h"
#include "aura/Renderer/Texture.h"

namespace aura3d
{
namespace mtl
{

/**
 * @class MtlTextureManager
 *
 * @brief Owns every texture the renderer has created, plus the one sampler they
 *        are all read through.
 *
 * The Metal counterpart of VkTextureManager + VkDescriptorManager, and much the
 * smaller of the two: Metal binds a texture straight to a fragment argument
 * slot, so there is no descriptor set to allocate, no pool to size and no
 * bindless table to index (VulkanRenderer's _bindlessTextureSet) --
 * MetalRenderer's bindTexture() is one setFragmentTexture() call.
 *
 * Handles are 1-based and dense, matching the vertex/index managers.
 */
class MtlTextureManager
{
  public:
    /**
     * @brief Creates the shared sampler state on @p device.
     *
     * @throws AuraException if @p device is null or the sampler cannot be created.
     */
    explicit MtlTextureManager(MTL::Device *device);

    ~MtlTextureManager();

    MtlTextureManager(const MtlTextureManager &) = delete;
    MtlTextureManager &operator=(const MtlTextureManager &) = delete;

    /**
     * @brief Uploads tightly packed RGBA8 pixels as a sampleable texture.
     *
     * @param[in] rgbaPixels @p width * @p height * 4 bytes, RGBA order.
     * @param[in] width Texture width in pixels.
     * @param[in] height Texture height in pixels.
     * @return A 1-based handle, or an invalid handle on failure.
     */
    [[nodiscard]] TextureHandle createFromPixels(const u8 *rgbaPixels, u32 width, u32 height);

    /// @p desc's format and sampler; a mip chain is generated on @p queue, which this waits for.
    [[nodiscard]] TextureHandle createTexture(const TextureDesc &desc, std::span<const std::byte> pixels,
                                              MTL::CommandQueue *queue);

    /**
     * @brief Allocates an RGBA8 texture whose contents are meant to change,
     *        initialised to transparent black.
     *
     * Distinct from createFromPixels() only in that the caller intends to keep
     * writing to it through updateRegion(); on Metal both are shared-storage
     * textures that replaceRegion() can target, so the distinction costs nothing
     * here. It exists because IRenderer draws it (a growing glyph atlas must not
     * be re-created per new character) and because the Vulkan backend genuinely
     * needs the two paths kept apart.
     *
     * @param[in] width Texture width in pixels.
     * @param[in] height Texture height in pixels.
     * @return A 1-based handle, or an invalid handle on failure.
     */
    [[nodiscard]] TextureHandle createDynamic(u32 width, u32 height);

    //! Zero-initialized linear R8 coverage, sampled as (1, 1, 1, coverage).
    [[nodiscard]] TextureHandle createCoverage(u32 width, u32 height);
    bool updateCoverageRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8 *coverage);

    /**
     * @brief Overwrites a sub-rectangle of an existing texture in place.
     *
     * @param[in] handle Texture to write into.
     * @param[in] x Left edge of the destination rectangle, in pixels.
     * @param[in] y Top edge of the destination rectangle, in pixels.
     * @param[in] width Rectangle width in pixels.
     * @param[in] height Rectangle height in pixels.
     * @param[in] rgbaPixels Tightly packed @p width * @p height * 4 bytes.
     * @return false when @p handle is unknown, when the rectangle reaches
     *         outside the texture (rejected rather than clamped, as IRenderer
     *         specifies), or when @p rgbaPixels is null. Each case is logged.
     */
    bool updateRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8 *rgbaPixels);

    /**
     * @brief Resolves @p handle to its texture.
     * @return The texture, or nullptr for a handle that was never created.
     */
    [[nodiscard]] MTL::Texture *resolve(TextureHandle handle) const noexcept;

    /// A render target's colour: private storage, sampled through the shared clamped sampler.
    //! A valid @p replace gets the new texture in its slot, keeping its handle.
    [[nodiscard]] TextureHandle createAttachment(u32 width, u32 height, TextureFormat format,
                                                 TextureHandle replace = {});

    /// The sampler @p handle is read through: its own when createTexture() made it, else the shared
    /// linear, clamp-to-edge one.
    [[nodiscard]] MTL::SamplerState *samplerFor(TextureHandle handle) const noexcept;

    //! Releases every texture (the sampler outlives them). Every handle issued
    //! before this becomes stale.
    void clear() noexcept;

    //! Number of textures currently held, for logging.
    [[nodiscard]] size_t count() const noexcept
    {
        return _textures.size();
    }

  private:
    /**
     * @brief Allocates a shared-storage RGBA8 texture.
     * @return The texture, or an empty handle on failure (logged).
     */
    [[nodiscard]] NS::SharedPtr<MTL::Texture> allocate(u32 width, u32 height, const char *label,
                                                       bool coverageOnly = false) const;
    bool updatePixels(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8 *pixels, bool coverageOnly);

    MTL::Device *_device = nullptr; //! Borrowed; owned by MtlDeviceManager.

    NS::SharedPtr<MTL::SamplerState> _sampler;

    //! Handle N lives at index N - 1; see MtlVertexBufferManager::_buffers.
    std::vector<NS::SharedPtr<MTL::Texture>> _textures;
    //! Parallel to _textures; empty where the texture uses the shared _sampler.
    std::vector<NS::SharedPtr<MTL::SamplerState>> _samplers;
};

} // namespace mtl
} // namespace aura3d

#endif // MTLTEXTUREMANAGER_H
