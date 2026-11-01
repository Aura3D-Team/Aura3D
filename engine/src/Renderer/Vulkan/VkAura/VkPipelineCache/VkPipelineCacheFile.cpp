#include "aura/Renderer/Vulkan/VkAura/VkPipelineCache/VkPipelineCacheFile.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

#include "aura/Core/AuraException/AuraException.h"
#include "aura/Renderer/Vulkan/VkAura/VkAuraCore.h"

namespace aura3d
{
namespace vk
{

namespace
{

//! Drivers check the header themselves, but some crash on a foreign blob rather than reject it.
[[nodiscard]] bool writtenBy(const std::vector<char> &data, const VkPhysicalDeviceProperties &properties) noexcept
{
    VkPipelineCacheHeaderVersionOne header{.headerVersion = VK_PIPELINE_CACHE_HEADER_VERSION_ONE};
    if (data.size() < sizeof(header))
        return false;
    std::memcpy(&header, data.data(), sizeof(header));
    return header.headerSize >= sizeof(header) && header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
           header.vendorID == properties.vendorID && header.deviceID == properties.deviceID &&
           std::memcmp(header.pipelineCacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE) == 0;
}

} // namespace

VkPipelineCacheFile::VkPipelineCacheFile(VkDevice device, VkPhysicalDevice physicalDevice, std::string path)
    : _device(device), _path(std::move(path))
{
    vkGetPhysicalDeviceProperties(physicalDevice, &_properties);

    std::vector<char> data;
    if (!_path.empty())
    {
        std::ifstream file(_path, std::ios::binary);
        data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        if (!data.empty() && !writtenBy(data, _properties))
        {
            INK_INFO << "Vulkan pipeline cache " << _path << " is from another device or driver; starting empty";
            data.clear();
        }
    }

    const VkPipelineCacheCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        .initialDataSize = data.size(),
        .pInitialData = data.empty() ? nullptr : data.data(),
    };
    VK_RESULT_CHECK(vkCreatePipelineCache(_device, &info, nullptr, &_cache));
    INK_VERBOSE << "Vulkan pipeline cache: " << data.size() << " bytes loaded";
}

VkPipelineCacheFile::~VkPipelineCacheFile()
{
    save();
    vkDestroyPipelineCache(_device, _cache, nullptr);
}

VkPipelineCache VkPipelineCacheFile::handle() const noexcept
{
    return _cache;
}

void VkPipelineCacheFile::save() const noexcept
{
    if (_path.empty() || _cache == VK_NULL_HANDLE)
        return;
    try
    {
        size_t size = 0;
        if (vkGetPipelineCacheData(_device, _cache, &size, nullptr) != VK_SUCCESS || size == 0)
            return;
        std::vector<char> data(size);
        if (vkGetPipelineCacheData(_device, _cache, &size, data.data()) != VK_SUCCESS)
            return;

        const std::filesystem::path target(_path);
        std::error_code error;
        if (target.has_parent_path())
            std::filesystem::create_directories(target.parent_path(), error);
        const std::filesystem::path temporary = target.string() + ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file.write(data.data(), static_cast<std::streamsize>(size));
            if (!file)
            {
                INK_WARN << "Could not write the Vulkan pipeline cache to " << temporary.string();
                return;
            }
        }
        std::filesystem::rename(temporary, target, error);
        if (error)
            INK_WARN << "Could not replace the Vulkan pipeline cache " << _path << ": " << error.message();
    }
    catch (const std::exception &exception)
    {
        INK_WARN << "Could not save the Vulkan pipeline cache: " << exception.what();
    }
}

} // namespace vk
} // namespace aura3d
