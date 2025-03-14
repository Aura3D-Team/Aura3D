#ifndef VKTEXTUREMANAGER_H
#define VKTEXTUREMANAGER_H

#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>

namespace aura3d {

class VkBufferMemoryAllocator;

class VkTextureManager {
public:
    // Struct to hold texture data
    struct TextureData {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView view;
        VkSampler sampler;
        uint32_t width;
        uint32_t height;
    };

    // Constructor
    VkTextureManager(VkDevice* device,
                     VkPhysicalDevice* physicalDevice,
                     VkCommandPool commandPool,
                     VkQueue graphicsQueue,
                     VkBufferMemoryAllocator* bufferAllocator);

    // Destructor
    ~VkTextureManager();

    // Create a solid color texture (1x1 pixel)
    TextureData createSolidColorTexture(const std::string& name,
                                        uint8_t r, uint8_t g, uint8_t b,
                                        uint8_t a = 255);

    // Get a texture by name
    const TextureData* getTexture(const std::string& name) const;

    // Clean up resources
    void cleanup();

private:
    VkDevice* _device;
    VkPhysicalDevice* _physicalDevice;
    VkCommandPool _commandPool;
    VkQueue _graphicsQueue;
    VkBufferMemoryAllocator* _bufferAllocator;

    // Storage for textures by name
    std::unordered_map<std::string, TextureData> _textures;

    // Create an image with the given properties
    void createImage(uint32_t width,
                     uint32_t height,
                     VkFormat format,
                     VkImage& image,
                     VkDeviceMemory& imageMemory);

    // Create an image view for the given image
    VkImageView createImageView(VkImage image, VkFormat format);

    // Create a sampler
    VkSampler createSampler();

    // Helper functions
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
};

} // namespace aura3d

#endif // VKTEXTUREMANAGER_H
