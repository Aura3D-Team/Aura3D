#ifndef VKINSTANCEMANAGER_H
#define VKINSTANCEMANAGER_H

#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>

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
    VkInstanceManager(VkInstanceData vkInstanceData);

    /**
     * Destructor.
     *
     * Destroys the Vulkan instance when the VkInstanceManager object is destroyed,
     * ensuring proper cleanup of Vulkan resources.
     */
    ~VkInstanceManager();

    /**
     * Returns a pointer to the Vulkan instance.
     *
     * This function provides access to the Vulkan instance handle, which can be
     * used in other parts of the application to perform Vulkan operations.
     *
     * @return VkInstance* - Pointer to the Vulkan instance.
     */
    VkInstance* getVkInstance();

private:
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
     * Vulkan instance creation information.
     *
     * This struct contains the configuration needed to create a Vulkan instance,
     * including references to the application info, validation layers, and
     * extensions.
     */
    VkInstanceCreateInfo _instanceInfo;

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
};

#endif // VKINSTANCEMANAGER_H
