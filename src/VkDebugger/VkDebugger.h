#ifndef VKDEBUGGER_H
#define VKDEBUGGER_H

#pragma once

#include <vulkan/vulkan.h>

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
     * @brief Creates the Vulkan debug messenger.
     *
     * This function initializes the debug messenger using the provided
     * `VkDebugUtilsMessengerCreateInfoEXT` structure, allowing Vulkan to send debug
     * and validation messages to the specified callback.
     *
     * @param pAllocator Optional custom allocator, or nullptr for the default allocator.
     *
     * @return VkResult Result of the creation call (VK_SUCCESS if successful).
     */
    VkResult createDebugUtilsMessengerEXT(VkAllocationCallbacks* pAllocator);

    /**
     * @brief Retrieves a pointer to the debug messenger.
     *
     * Provides access to the created debug messenger, allowing it to be used
     * or referenced externally if needed.
     *
     * @return VkDebugUtilsMessengerEXT* A pointer to the Vulkan debug messenger.
     */
    VkDebugUtilsMessengerEXT* getVkDebugMessenger();

    /**
     * @brief Retrieves a pointer to the debug creation info.
     *
     * Provides access to the created debug info, allowing it to be accessed externally
     *
     * @return VkDebugUtilsMessengerCreateInfoEXT* A pointer to the Vulkan debug messenger.
     */
    VkDebugUtilsMessengerCreateInfoEXT* getVkDebugInfo();

private:
    VkInstance* _vkInstance; ///< Pointer to the Vulkan instance associated with this debug messenger.

    VkDebugUtilsMessengerEXT _debugMessenger; ///< The Vulkan debug messenger handle.

    VkDebugUtilsMessengerCreateInfoEXT _debugInfo; ///< The create info structure with settings for the debug messenger.
};

#endif // VKDEBUGGER_H
