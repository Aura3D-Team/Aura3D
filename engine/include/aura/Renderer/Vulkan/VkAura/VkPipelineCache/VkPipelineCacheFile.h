#ifndef VKPIPELINECACHEFILE_H
#define VKPIPELINECACHEFILE_H

#pragma once

#include <string>

#include "aura/Renderer/Vulkan/VkAura/VkLoader/VkLoader.h"

namespace aura3d
{
namespace vk
{

/// A VkPipelineCache kept on disk between runs, so a second start skips the driver's shader compiles.
class VkPipelineCacheFile
{
  public:
    /// Seeded from @p path when it holds a cache this device and driver wrote; anything else starts empty.
    /// An empty @p path keeps the cache in memory only.
    VkPipelineCacheFile(VkDevice device, VkPhysicalDevice physicalDevice, std::string path);
    /// Saves, then destroys the cache: the device must still be alive.
    ~VkPipelineCacheFile();

    VkPipelineCacheFile(const VkPipelineCacheFile &) = delete;
    VkPipelineCacheFile &operator=(const VkPipelineCacheFile &) = delete;

    [[nodiscard]] VkPipelineCache handle() const noexcept;

    /// Writes the cache through a temporary file, so a crash mid-write leaves the old one intact.
    /// Failures are logged: a missing cache only costs compile time.
    void save() const noexcept;

  private:
    VkDevice _device;
    VkPhysicalDeviceProperties _properties{};
    std::string _path;
    VkPipelineCache _cache = VK_NULL_HANDLE;
};

} // namespace vk
} // namespace aura3d

#endif // VKPIPELINECACHEFILE_H
