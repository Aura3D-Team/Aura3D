#ifndef VKINSTANCEMANAGER_H
#define VKINSTANCEMANAGER_H

#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <memory>

#include <VkAura/VkDebugger/VkDebugger.h>
#include <VkAura/VkHostAllocator/VkHostAllocator.h>
#include <VkAura/VkDeviceAllocator/VkDeviceAllocator.h>

namespace aura3d {

/**
 * @brief This struct represents Important data for VkInstance creation
 *
 * Obs: for more details, it can have more parameters in the future
 */
struct VkInstanceData {
    const char* appName;
    const char* engineName;
    std::vector<int> appVersion;
    std::vector<const char*> vkInstanceExtensions;
    std::vector<const char*> vkValidationLayers;
};

/**
 * @class to manage Vulkan instance creation, configuration, and destruction.
 *
 * This class is responsible for:
 * - Initializing the Vulkan instance with the necessary application info.
 * - Enabling Vulkan extensions and validation layers (if required).
 * - Handling the cleanup of the Vulkan instance when it is no longer needed.
 *
 * The Vulkan instance is the first object created in a Vulkan application and is used
 * to establish the connection between the application and the Vulkan runtime. This
 * manager class helps abstract the complexity of Vulkan instance creation.
 */
class VkInstanceManager
{
public:
    /**
     * Constructor.
     *
     * Initializes the Vulkan instance by setting up application information,
     * validation layers, and required instance extensions.
     * If the Vulkan instance cannot be created, it throws an exception.
     */
    VkInstanceManager(VkHostAllocator* vkHostAllocator,
                      VkInstanceData vkInstanceData,
                      bool enableValidationLayers);

    /**
     * Destructor.
     *
     * Destroys the Vulkan instance when the VkInstanceManager object is destroyed,
     * ensuring proper cleanup of Vulkan resources.
     */
    ~VkInstanceManager();

    /**
     * @brief Initializes the Vulkan application information.
     *
     * Populates `_appInfo` with data from the provided `VkInstanceData` struct, including
     * application name, engine name, and version information. This function ensures that
     * `_appInfo` is correctly configured before creating the Vulkan instance.
     *
     * @param vkInstanceData Structure containing the application-specific information.
     */
    void initializeAppInfo(const VkInstanceData& vkInstanceData);

    /**
     * @brief Initializes the Vulkan instance creation information.
     *
     * Sets up `_instanceInfo`, linking it with `_appInfo` and preparing it for Vulkan
     * instance creation. This function also initializes other essential Vulkan instance
     * properties.
     */
    void initializeInstanceInfo();

    /**
     * @brief Validates and filters instance extensions for compatibility.
     *
     * Checks that each extension in `_vkInstanceExtensions` is supported by the Vulkan
     * implementation. Unsupported extensions are removed from the list, and a warning is
     * logged if any are not supported.
     *
     * @throws AuraException if any required instance extensions are not available.
     */
    void validateInstanceExtensions();

    /**
     * @brief Validates and filters validation layers for compatibility.
     *
     * Checks that each requested validation layer in `_vkValidationLayers` is supported by
     * the Vulkan implementation. If unsupported layers are found, a warning is logged, and
     * the function throws `AuraException`.
     *
     * @throws AuraException if any required validation layers are not available.
     */
    void validateValidationLayers();

    /**
     * @brief Prepares the debug messenger configuration if validation layers are enabled.
     *
     * Checks for support of the `VK_EXT_DEBUG_UTILS_EXTENSION_NAME` extension and, if available,
     * adds it to `_vkInstanceExtensions`. If the extension is not available and validation
     * layers are enabled, logs a warning.
     *
     * @param enableValidationLayers Indicates whether validation layers are enabled.
     * @return VK_SUCCESS if the extension is supported; otherwise, an appropriate Vulkan error code.
     */
    VkResult prepareVkDebugger(const bool enableValidationLayers);

    /**
     * @brief Creates the Vulkan debug messenger instance.
     *
     * Initializes `_vkDebugger` and sets up the debug messenger using the provided `debugCreateInfo`.
     * Throws an exception if the debug messenger creation fails.
     *
     * @param debugCreateInfo Pointer to the debug messenger creation info structure.
     * @throws AuraException if the debug messenger cannot be created.
     */
    void createDebuggerInstance(VkDebugUtilsMessengerCreateInfoEXT* debugCreateInfo);


    /**
     * Returns a pointer to the Vulkan instance.
     *
     * This function provides access to the Vulkan instance handle, which can be
     * used in other parts of the application to perform Vulkan operations.
     *
     * @return VkInstance* - Pointer to the Vulkan instance.
     */
    VkInstance* getVkInstance();

    /**
     * Returns a pointer to the Vulkan instance creation info.
     *
     * This function provides access to the Vulkan app creation info, which can be
     * be accessed for checks and debug creation.
     *
     * @return VkApplicationInfo* - Pointer to the Vulkan instance creation info.
     */
    VkApplicationInfo* getAppInfo();

    /**
     * @brief Retrieves the VkDebugger instance.
     *
     * This function returns a pointer to the `VkDebugger` instance associated
     * with this Vulkan setup. The `VkDebugger` provides mechanisms for setting up
     * and managing debug callbacks, allowing the capture of validation layer messages
     * for debugging and logging Vulkan operations.
     *
     * @return VkDebugger* - Pointer to the `VkDebugger` instance, or nullptr if debugging is disabled.
     *
     * @see VkDebugger for more details.
     */
    std::unique_ptr<VkDebugger>* getVkDebugger();

private:
    VkHostAllocator* vkHosAllocator;

    /**
     * Vulkan instance handle.
     *
     * The Vulkan instance is a handle that represents the connection between the
     * application and the Vulkan library. It is initialized during construction
     * and destroyed in the destructor.
     */
    VkInstance _vkInstance;

    /**
     * Vulkan application information.
     *
     * This struct provides details about the application, such as its name,
     * version, and the Vulkan API version it intends to use. It is passed
     * during the creation of the Vulkan instance.
     */
    VkApplicationInfo _appInfo;

    /**
     * @brief Vulkan instance creation information.
     *
     * The `_instanceInfo` member holds configuration details required for creating
     * a Vulkan instance, including application information, validation layers, and
     * instance extensions.
     *
     * - `VkApplicationInfo* pApplicationInfo`: Points to the application-specific information
     *   such as application name, engine name, and API version.
     * - `const char* const* ppEnabledLayerNames`: List of validation layers enabled for debugging
     *   purposes. Layers are typically enabled in debug mode and may include
     *   standard validation layers like "VK_LAYER_KHRONOS_validation".
     * - `const char* const* ppEnabledExtensionNames`: List of extensions required by the instance,
     *   which may include extensions for cross-platform surface compatibility
     *   (`VK_KHR_surface`) or debugging support (`VK_EXT_debug_utils`).
     * - `void* pNext`: Optional pointer to additional creation details or structures,
     *   such as debug messenger creation info when validation layers are active.
     *
     * This structure is populated before instance creation and passed to `vkCreateInstance`.
     */
    VkInstanceCreateInfo _instanceInfo;

    /**
     * @brief Debugging support for Vulkan instance.
     *
     * `_vkDebugger` is a pointer to a `VkDebugger` object that manages Vulkan debug
     * utilities. It provides support for capturing debug messages and validation
     * layer feedback, which are helpful during development to diagnose potential
     * issues with Vulkan API usage.
     *
     * - If validation layers are enabled, `_vkDebugger` is initialized and configured
     *   with a debug messenger.
     * - `_vkDebugger` is responsible for creating and destroying the `VkDebugUtilsMessengerEXT`
     *   associated with the Vulkan instance.
     * - The debug messenger captures messages from Vulkan's validation layers and
     *   outputs them through a custom callback function, typically logging warnings
     *   and errors.
     *
     * `_vkDebugger` must be destroyed before the Vulkan instance, as the debug
     * messenger is tied to the instance's lifecycle.
     */
    std::unique_ptr<VkDebugger> _vkDebugger;

    /**
     * @brief Configuration for Vulkan debug messaging.
     *
     * Holds the settings and parameters for creating the Vulkan debug messenger,
     * which captures validation and debugging messages from the Vulkan API.
     * This structure is used to specify the types of messages to capture, the
     * severity of messages, and the callback function that handles them.
     *
     * @see VkDebugger::setupDebugMessenger() for initialization details.
     */
    VkDebugUtilsMessengerCreateInfoEXT _debugCreateInfo;

    /**
     * List of validation layers to enable.
     *
     * Validation layers provide debugging information and are useful during
     * development. This vector holds the names of the validation layers to be
     * enabled when creating the Vulkan instance.
     */
    std::vector<const char*> _vkValidationLayers;

    /**
     * List of required instance extensions.
     *
     * Instance extensions are used to enable additional functionality in Vulkan.
     * This vector holds the names of the extensions that are needed by the
     * application, such as surface creation extensions for rendering windows.
     */
    std::vector<const char*> _vkInstanceExtensions;

    /**
     * @brief Check extension support for the instance.
     *
     * @param exts Extensions supposed to be used for this app.
     * @return VkResult Result format for vulkan error code.
     */
    VkResult _checkInstanceExtensionSupport(const std::vector<const char*>& exts) const;

    /**
     * @brief Check validation layer support for the instance.
     *
     * @param validationLayers Validation layers supposed to be used for this app.
     * @return VkResult Result format for vulkan error code.
     */
    VkResult _checkValidationLayerSupport(const std::vector<const char*>& validationLayers) const;
};

}

#endif // VKINSTANCEMANAGER_H
