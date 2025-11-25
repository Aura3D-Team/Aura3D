#ifndef VKDEBUGGER_H
#define VKDEBUGGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <string>

namespace aura3d {
namespace vk {

/**
 * @brief The VkDebugger class
 *
 * This class sets up and manages a Vulkan debug messenger, allowing for detailed logging
 * and error checking during Vulkan API calls. It utilizes Vulkan's `VK_EXT_debug_utils`
 * extension to register a debug callback, which provides messages on validation errors,
 * performance issues, and general debug information.
 */
class VkDebugger
{
public:
    /**
     * @brief Constructs a VkDebugger with the specified Vulkan instance.
     *
     * Initializes the VkDebugger by associating it with a Vulkan instance.
     * It prepares the debugger for setting up a debug messenger and managing
     * validation messages.
     * Also, configures the `VkDebugUtilsMessengerCreateInfoEXT` structure with appropriate
     * flags for message severity and message type, preparing it for use when creating
     * the debug messenger.
     *
     * @param vkInstance A pointer to the Vulkan instance with which to associate the debug messenger.
     */
    VkDebugger(VkInstance* vkInstance);

    /**
     * @brief Destructor for VkDebugger.
     *
     * Cleans up by destroying the debug messenger if it was created, ensuring
     * that resources are properly released.
     */
    ~VkDebugger();

    /**
     * @brief The debug callback function.
     *
     * A callback function that handles validation and debugging messages from Vulkan.
     * The callback is registered with Vulkan to receive messages about issues like validation
     * errors, warnings, and performance hints.
     *
     * @param messageSeverity The severity of the message (e.g., info, warning, error).
     * @param messageType The type of message (e.g., general, validation, performance).
     * @param pCallbackData A pointer to additional data describing the message.
     * @param pUserData Optional user data passed to the callback.
     *
     * @return VK_FALSE Always returns VK_FALSE to indicate that Vulkan should not abort.
     */
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData
        );

    /**
     * @brief Sets up configuration information for Vulkan's debug messenger.
     *
     * The `setupDebugMessenger` function configures a `VkDebugUtilsMessengerCreateInfoEXT` structure
     * with the desired message severity levels, message types, and the callback function used for
     * handling debug messages.
     *
     * This structure is used during Vulkan instance creation to enable validation layer messages.
     * It can also be used when creating the debug messenger explicitly after the instance has been
     * created.
     *
     * @return A `VkDebugUtilsMessengerCreateInfoEXT` structure initialized with settings for
     *         capturing and handling debug messages from Vulkan's validation layers.
     */
    static VkDebugUtilsMessengerCreateInfoEXT setupDebugMessenger();

    /**
     * @brief Creates the Vulkan debug messenger.
     *
     * This function initializes the debug messenger using the provided
     * `VkDebugUtilsMessengerCreateInfoEXT` structure, allowing Vulkan to send debug
     * and validation messages to the specified callback.
     *
     * @param debugInfo The configuration information for Vulkan's debug messenger.
     * @param pAllocator Optional custom allocator, or nullptr for the default allocator.
     *
     * @return VkResult Result of the creation call (VK_SUCCESS if successful).
     */
    VkResult createDebugUtilsMessengerEXT(VkDebugUtilsMessengerCreateInfoEXT* debugInfo, VkAllocationCallbacks* pAllocator);

    /**
     * @brief Retrieves a pointer to the debug messenger.
     *
     * Provides access to the created debug messenger, allowing it to be used
     * or referenced externally if needed.
     *
     * @return VkDebugUtilsMessengerEXT* A pointer to the Vulkan debug messenger.
     */
    VkDebugUtilsMessengerEXT* getVkDebugMessenger();

private:
    /**
     * @brief Convert VkDebugUtilsMessageSeverityFlagBitsEXT to string
     */
    static std::string severityToString(VkDebugUtilsMessageSeverityFlagBitsEXT severity);

    /**
     * @brief Convert VkDebugUtilsMessageTypeFlagsEXT to string
     */
    static std::string messageTypeToString(VkDebugUtilsMessageTypeFlagsEXT type);

    /**
     * @brief Format object information from debug callback data
     */
    static std::string formatObjectInfo(const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData);

    /**
     * @brief Format label information from debug callback data
     */
    static std::string formatLabelInfo(const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData);

    VkInstance* _vkInstance; ///< Pointer to the Vulkan instance associated with this debug messenger.
    VkDebugUtilsMessengerEXT _debugMessenger; ///< The Vulkan debug messenger handle.
};

}
} // namespace aura3d

#endif // VKDEBUGGER_H
