#ifndef VKDEVICEMANAGER_H
#define VKDEVICEMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>

#include "aura/Renderer/Vulkan/VkAura/VkAuraCore.h"
#include "aura/Renderer/Vulkan/VkAura/VkQueueManager/VkQueueManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkSurfaceManager/VkSurfaceManager.h"
namespace aura3d {
namespace vk {

/**
 * @class VkDeviceManager
 *
 * @brief Manages Vulkan physical and logical devices.
 *
 * This class is responsible for selecting the best available physical device,
 * creating a logical device, and managing device resources. It integrates with
 * Vulkan instance by taking a `VkInstance` and working with it to enumerate and choose
 * the best physical device available.
 */
class VkDeviceManager
{
public:
    /**
     * @brief Constructor that initializes the device manager with a Vulkan instance.
     *
     * @param vkInstance The Vulkan instance to use for device management.
     */
    VkDeviceManager(VkInstance* vkInstance, VkDeviceData vkDeviceData);

    /**
     * @brief Destructor that cleans up the logical device.
     *
     * Cleans up and destroys the Vulkan logical device created by the manager.
     */
    ~VkDeviceManager();

    /**
     * @brief Retrieves a pointer to Vulkan logical device.
     *
     * This method returns the Vulkan logical device (`VkDevice`) created by the manager,
     * which is used to perform operations such as drawing, computing, and rendering.
     * The logical device is configured with the specific queues needed for these operations.
     *
     * @return VkDevice The Vulkan logical device that has been created with specific queue configurations.
     */
    VkDevice* getDevice();

    /**
     * @brief Retrieves a pointer to the Vulkan physical device.
     *
     * This method returns the Vulkan physical device (`VkPhysicalDevice`) that was selected and used
     * to create the logical device. The physical device represents the actual GPU or hardware
     * selected based on the criteria defined in the `setBestDevice` method.
     *
     * @return VkPhysicalDevice The Vulkan physical device that was chosen to create the logical device with specific queues.
     */
    VkPhysicalDevice* getPhysicalDevice();

    VkDeviceData* getDeviceCreationData();

    /**
     * @brief See if physical device supports Vulkan surface operations
     * @param vkSurfaceManager The VkSurfaceManager
     * @param flags The VkQueueFlags
     * @return VkBool32 true if physical device supports Vulkan surface operations
     */
    VkBool32 physicalDeviceHasQueueSurfaceSupport(VkSurfaceManager vkSurfaceManager, const VkQueueFlags flags);

    VkQueueManager* getQueueManager();

    /**
     * @brief Largest MSAA sample count the selected device supports for both
     *        the color and depth attachments together.
     * @return The highest common VkSampleCountFlagBits, or VK_SAMPLE_COUNT_1_BIT
     *         if the device reports no multisampling support.
     */
    [[nodiscard]] VkSampleCountFlagBits getMaxUsableSampleCount() const;

    /**
     * @brief Whether the selected physical device actually supports the
     * Vulkan 1.2 bufferDeviceAddress feature.
     *
     * Checked via vkGetPhysicalDeviceFeatures2 before requesting it at
     * device-creation time, requesting a feature a device doesn't support
     * isn't reliably rejected by every driver at vkCreateDevice (especially
     * without validation layers, which aren't available on Android unless
     * specially bundled into the APK), so blindly trusting a "true" from
     * settings.json here would leave VulkanMemoryManager creating its
     * allocator with VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT set for
     * a device that never actually turned the feature on -- VMA then
     * asserts/aborts the first time it needs it, rather than the failure
     * surfacing as a clean Vulkan error at device-creation time.
     *
     * @return true if bufferDeviceAddress was both supported and enabled.
     */
    [[nodiscard]] bool supportsBufferDeviceAddress() const { return _bufferDeviceAddressSupported; }

    /**
     * @brief Whether the selected physical device supports the three Vulkan 1.2
     * descriptor-indexing features the bindless texture array (VulkanRenderer's
     * set 1) needs: descriptorBindingPartiallyBound, runtimeDescriptorArray and
     * descriptorBindingSampledImageUpdateAfterBind.
     *
     * Always true once the constructor returns: unlike bufferDeviceAddress,
     * there is no fallback path for this feature set, so _setBestDevice()
     * throws AuraException immediately if the selected device lacks one
     * rather than leaving a VkDeviceManager whose caller can be surprised
     * by it later. Kept as a query (rather than just documenting the
     * precondition) so callers can still assert/log the reason explicitly.
     *
     * @return true if all three features were supported and enabled.
     */
    [[nodiscard]] bool supportsBindlessTextures() const { return _bindlessTexturesSupported; }

    /**
     * @brief Number of texture slots the bindless table may actually declare
     *        on this device.
     *
     * kDesiredBindlessTextures clamped by every update-after-bind limit a
     * combined-image-sampler array consumes. This is the *only* correct size
     * to build the descriptor set layout, size the descriptor pool, or bounds
     * check a texture index against -- using the unclamped constant instead
     * fails at vkCreateDescriptorSetLayout on any device whose limits are
     * lower than it (mobile drivers, in practice).
     *
     * Guaranteed >= kMinBindlessTextures: a device that cannot host even
     * that is rejected at selection time.
     */
    [[nodiscard]] u32 maxBindlessTextures() const { return _maxBindlessTextures; }

private:
    VkInstance* _vkInstance; ///< Vulkan instance pointer used bind the best device

    VkDeviceData _vkDeviceCreationData; ///< This struct represents Important data for VkDevice creation
    VkDeviceCreateInfo _deviceInfo;  ///< Information required to create the logical device
    VkDevice _device;  ///< Handle to the Vulkan logical device

    VkPhysicalDevice _physicalDevice;  ///< Handle to the selected Vulkan physical device
    VkPhysicalDeviceProperties _deviceProperties; ///< Store best physical device properties
    VkPhysicalDeviceFeatures _deviceFeatures; ///< Store best physical device features
    bool _bufferDeviceAddressSupported = false; ///< See supportsBufferDeviceAddress()
    bool _bindlessTexturesSupported = false; ///< See supportsBindlessTextures()
    u32 _maxBindlessTextures = 0; ///< See maxBindlessTextures()
    u32 _physicaldeviceCount;  ///< Number of available physical devices

    VkQueueManager _vkQueueManager; ///< Queue manager to create queues in a organized way.

    /**
     * @brief Sets the best physical and logical device.
     *
     * This function enumerates the physical devices available and selects the best one
     * based on the required criteria (e.g., queue family support). It then creates a logical
     * device from the selected physical device.
     *
     * @param vkInstance Vulkan instance to set the device
     *
     * Throws a `AuraException` if any Vulkan operation fails during device selection or creation.
     */
    void _setBestDevice(VkInstance vkInstance);

    /**
     * @brief Check extension support for the the physical device.
     *
     * @param exts Extensions supposed to be used for the final logic device.
     * @return VkResult Result format for vulkan error code.
     */
    VkResult _checkDeviceExtensionSupport(std::vector<const char*> exts) const;

    /**
     * @brief Resolves maxBindlessTextures() against the selected device's
     *        update-after-bind descriptor limits.
     *
     * Called once from _setBestDevice(), after the physical device is chosen
     * and its descriptor-indexing feature support confirmed.
     *
     * @return The clamped slot count, always >= kMinBindlessTextures.
     * @throws AuraException if the device's limits cannot host even that.
     */
    [[nodiscard]] u32 _queryMaxBindlessTextures() const;
};

}
}

#endif // VKDEVICEMANAGER_H
