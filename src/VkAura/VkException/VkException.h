#ifndef VKEXCEPTION_H
#define VKEXCEPTION_H

#pragma once

#include <stdexcept>
#include <unordered_map>
#include <vulkan/vulkan.h>

/**
 * @class VkException
 *
 * Custom exception class to handle Vulkan-related errors.
 *
 * This class extends `std::exception` and is designed to handle Vulkan-specific error codes (`VkResult`).
 * It provides constructors that accept error codes or custom messages and can be used to throw and catch
 * Vulkan-related exceptions. The error code can be mapped to a human-readable string for easier debugging
 * and logging.
 */
class VkException : public std::exception
{
public:
    /**
     * Default constructor.
     *
     * Creates a `VkException` with no specific message.
     * The default message will be "Unknown Vulkan exception" if not set.
     */
    VkException() noexcept;

    /**
     * Destructor.
     *
     * Cleans up the exception object. No custom cleanup is necessary for this class.
     */
    virtual ~VkException() noexcept;

    /**
     * Constructor with custom message.
     *
     * Creates a `VkException` with a specified message. This can be used to throw an exception
     * with a custom error message.
     *
     * @param msg Custom error message.
     */
    VkException(const char* msg);

    /**
     * Constructor with Vulkan error code (`VkResult`).
     *
     * Creates a `VkException` using a Vulkan error code. The error code is mapped to a descriptive
     * error message using the static map `vkResultToString`. If the error code is not found in the
     * map, the message defaults to "Unknown Vulkan error code".
     *
     * @param code Vulkan result code (`VkResult`) representing the error.
     */
    VkException(const VkResult code);

    /**
     * Copy constructor.
     *
     * Creates a copy of another `VkException` object.
     */
    VkException(const VkException&) = default;

    /**
     * Assignment operator.
     *
     * Copies the contents of one `VkException` to another.
     */
    VkException& operator=(const VkException&) = default;

    /**
     * Move constructor.
     *
     * Moves a `VkException` object, transferring ownership of resources.
     */
    VkException(VkException&&) = default;

    /**
     * Move assignment operator.
     *
     * Moves the contents of one `VkException` to another, transferring ownership of resources.
     */
    VkException& operator=(VkException&&) = default;

    /**
     * Overrides the what() function from `std::exception`.
     *
     * This function provides the error message associated with the Vulkan exception.
     * The message can be either the custom message provided during exception construction or
     * the string corresponding to the Vulkan error code.
     *
     * @return const char* A string describing the Vulkan error.
     */
    virtual const char* what() const noexcept override;

private:
    /**
     * Error message associated with the Vulkan exception.
     *
     * This message is either set via a custom string or automatically mapped from a Vulkan error code.
     */
    const char* _msg;
};

/**
 * Static map of Vulkan result codes (`VkResult`) to human-readable error messages.
 *
 * This map contains most Vulkan error codes and their corresponding string descriptions.
 * It is used in the `VkException` constructor that takes a `VkResult` as input to convert
 * the result code into a meaningful error message.
 */
static const std::unordered_map<int64_t, const char*> vkResultToString = {
    { VK_NOT_READY, "A fence or query has not yet completed" },
    { VK_TIMEOUT, "A wait operation has not completed in the specified time" },
    { VK_EVENT_SET, "An event is signaled" },
    { VK_EVENT_RESET, "An event is unsignaled" },
    { VK_INCOMPLETE, "A return array was too small for the result" },
    { VK_ERROR_OUT_OF_HOST_MEMORY, "A host memory allocation has failed" },
    { VK_ERROR_OUT_OF_DEVICE_MEMORY, "A device memory allocation has failed" },
    { VK_ERROR_INITIALIZATION_FAILED, "Initialization of an object could not be completed" },
    { VK_ERROR_DEVICE_LOST, "The logical or physical device has been lost" },
    { VK_ERROR_MEMORY_MAP_FAILED, "Mapping of a memory object has failed" },
    { VK_ERROR_LAYER_NOT_PRESENT, "A requested layer is not present or could not be loaded" },
    { VK_ERROR_EXTENSION_NOT_PRESENT, "A requested extension is not supported" },
    { VK_ERROR_FEATURE_NOT_PRESENT, "A requested feature is not supported" },
    { VK_ERROR_INCOMPATIBLE_DRIVER, "The requested version of Vulkan is not supported by the driver" },
    { VK_ERROR_TOO_MANY_OBJECTS, "Too many objects of the type have already been created" },
    { VK_ERROR_FORMAT_NOT_SUPPORTED, "A requested format is not supported on this device" },
    { VK_ERROR_FRAGMENTED_POOL, "A pool allocation has failed due to fragmentation" },
    { VK_ERROR_UNKNOWN, "An unknown error has occurred" },
    { VK_ERROR_OUT_OF_POOL_MEMORY, "A pool memory allocation has failed" },
    { VK_ERROR_INVALID_EXTERNAL_HANDLE, "An external handle is not valid" },
    { VK_ERROR_FRAGMENTATION, "A descriptor pool creation has failed due to fragmentation" },
    { VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS, "An invalid opaque capture address was specified" },
    { VK_PIPELINE_COMPILE_REQUIRED, "Pipeline compile required" },
    { VK_ERROR_SURFACE_LOST_KHR, "A surface is no longer available" },
    { VK_ERROR_NATIVE_WINDOW_IN_USE_KHR, "The requested window is already connected to another surface" },
    { VK_SUBOPTIMAL_KHR, "A swapchain no longer matches the surface properties exactly" },
    { VK_ERROR_OUT_OF_DATE_KHR, "A surface has changed in such a way that it is no longer compatible" },
    { VK_ERROR_INCOMPATIBLE_DISPLAY_KHR, "The display used is incompatible with the current surface" },
    { VK_ERROR_VALIDATION_FAILED_EXT, "Validation failed" },
    { VK_ERROR_INVALID_SHADER_NV, "An invalid shader was found" },
    { VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR, "Image usage is not supported" },
    { VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR, "Video picture layout is not supported" },
    { VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR, "Video profile operation not supported" },
    { VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR, "Video profile format not supported" },
    { VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR, "Video profile codec not supported" },
    { VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR, "Video std version not supported" },
    { VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT, "Invalid DRM format modifier plane layout" },
    { VK_ERROR_NOT_PERMITTED_KHR, "Not permitted" },
    { VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT, "Full-screen exclusive mode lost" },
    { VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR, "Invalid video std parameters" },
    { VK_ERROR_COMPRESSION_EXHAUSTED_EXT, "Compression exhausted" },
    { VK_INCOMPATIBLE_SHADER_BINARY_EXT, "Incompatible shader binary" }
};

#endif // VKEXCEPTION_H
