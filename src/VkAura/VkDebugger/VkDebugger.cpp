// VkDebugger.cpp
#include "VkDebugger.h"
#include <ink/ink.hpp>
#include <sstream>
#include <iomanip>

namespace aura3d {

// Helper function to get a string name for VkObjectType
std::string getVkObjectTypeName(VkObjectType type) {
    switch (type) {
    case VK_OBJECT_TYPE_INSTANCE: return "VK_OBJECT_TYPE_INSTANCE";
    case VK_OBJECT_TYPE_PHYSICAL_DEVICE: return "VK_OBJECT_TYPE_PHYSICAL_DEVICE";
    case VK_OBJECT_TYPE_DEVICE: return "VK_OBJECT_TYPE_DEVICE";
    case VK_OBJECT_TYPE_QUEUE: return "VK_OBJECT_TYPE_QUEUE";
    case VK_OBJECT_TYPE_SEMAPHORE: return "VK_OBJECT_TYPE_SEMAPHORE";
    case VK_OBJECT_TYPE_COMMAND_BUFFER: return "VK_OBJECT_TYPE_COMMAND_BUFFER";
    case VK_OBJECT_TYPE_FENCE: return "VK_OBJECT_TYPE_FENCE";
    case VK_OBJECT_TYPE_DEVICE_MEMORY: return "VK_OBJECT_TYPE_DEVICE_MEMORY";
    case VK_OBJECT_TYPE_BUFFER: return "VK_OBJECT_TYPE_BUFFER";
    case VK_OBJECT_TYPE_IMAGE: return "VK_OBJECT_TYPE_IMAGE";
    case VK_OBJECT_TYPE_EVENT: return "VK_OBJECT_TYPE_EVENT";
    case VK_OBJECT_TYPE_QUERY_POOL: return "VK_OBJECT_TYPE_QUERY_POOL";
    case VK_OBJECT_TYPE_BUFFER_VIEW: return "VK_OBJECT_TYPE_BUFFER_VIEW";
    case VK_OBJECT_TYPE_IMAGE_VIEW: return "VK_OBJECT_TYPE_IMAGE_VIEW";
    case VK_OBJECT_TYPE_SHADER_MODULE: return "VK_OBJECT_TYPE_SHADER_MODULE";
    case VK_OBJECT_TYPE_PIPELINE_CACHE: return "VK_OBJECT_TYPE_PIPELINE_CACHE";
    case VK_OBJECT_TYPE_PIPELINE_LAYOUT: return "VK_OBJECT_TYPE_PIPELINE_LAYOUT";
    case VK_OBJECT_TYPE_RENDER_PASS: return "VK_OBJECT_TYPE_RENDER_PASS";
    case VK_OBJECT_TYPE_PIPELINE: return "VK_OBJECT_TYPE_PIPELINE";
    case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT: return "VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT";
    case VK_OBJECT_TYPE_SAMPLER: return "VK_OBJECT_TYPE_SAMPLER";
    case VK_OBJECT_TYPE_DESCRIPTOR_POOL: return "VK_OBJECT_TYPE_DESCRIPTOR_POOL";
    case VK_OBJECT_TYPE_DESCRIPTOR_SET: return "VK_OBJECT_TYPE_DESCRIPTOR_SET";
    case VK_OBJECT_TYPE_FRAMEBUFFER: return "VK_OBJECT_TYPE_FRAMEBUFFER";
    case VK_OBJECT_TYPE_COMMAND_POOL: return "VK_OBJECT_TYPE_COMMAND_POOL";
    default: return "UNKNOWN_OBJECT_TYPE";
    }
}

VkDebugger::VkDebugger(VkInstance* vkInstance)
    : _vkInstance(vkInstance), _debugMessenger(VK_NULL_HANDLE)
{
    // Empty
}

VkDebugger::~VkDebugger()
{
    PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(*_vkInstance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr && _debugMessenger != VK_NULL_HANDLE) {
        func(*_vkInstance, _debugMessenger, nullptr);
        _debugMessenger = VK_NULL_HANDLE;
        INK_DEBUG << "VkDebugger deleted";
    }
    _vkInstance = nullptr;
}

std::string VkDebugger::severityToString(VkDebugUtilsMessageSeverityFlagBitsEXT severity) {
    switch (severity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: return "VERBOSE";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: return "INFO";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: return "WARNING";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: return "ERROR";
        default: return "UNKNOWN";
    }
}

std::string VkDebugger::messageTypeToString(VkDebugUtilsMessageTypeFlagsEXT type) {
    std::vector<std::string> types;

    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
        types.push_back("GENERAL");
    }
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        types.push_back("VALIDATION");
    }
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        types.push_back("PERFORMANCE");
    }

    std::stringstream ss;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) ss << "|";
        ss << types[i];
    }

    return ss.str();
}

std::string VkDebugger::formatObjectInfo(const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData) {
    if (pCallbackData->objectCount == 0) {
        return "";
    }

    std::stringstream ss;
    ss << "Objects:";

    for (uint32_t i = 0; i < pCallbackData->objectCount; i++) {
        const auto& obj = pCallbackData->pObjects[i];
        ss << "\n  - Type: " << getVkObjectTypeName(obj.objectType)
           << ", Handle: 0x" << std::hex << std::setw(16) << std::setfill('0') << obj.objectHandle;

        if (obj.pObjectName) {
            ss << ", Name: " << obj.pObjectName;
        }
    }

    return ss.str();
}

std::string VkDebugger::formatLabelInfo(const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData) {
    std::stringstream ss;

    // Queue labels
    if (pCallbackData->queueLabelCount > 0) {
        ss << "\nQueue Labels:";
        for (uint32_t i = 0; i < pCallbackData->queueLabelCount; i++) {
            const auto& label = pCallbackData->pQueueLabels[i];
            ss << "\n  - " << label.pLabelName;
        }
    }

    // Command buffer labels
    if (pCallbackData->cmdBufLabelCount > 0) {
        ss << "\nCommand Buffer Labels:";
        for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; i++) {
            const auto& label = pCallbackData->pCmdBufLabels[i];
            ss << "\n  - " << label.pLabelName;
        }
    }

    return ss.str();
}

VKAPI_ATTR VkBool32 VKAPI_CALL VkDebugger::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData
)
{
    // Format the debug message with detailed information
    std::stringstream ss;
    ss << "[Vulkan] " << severityToString(messageSeverity) << " [" << messageTypeToString(messageType) << "] ";

    // Add message ID if available
    if (pCallbackData->pMessageIdName) {
        ss << "[ " << pCallbackData->pMessageIdName << " ] ";
    }

    // Add object information
    std::string objectInfo = formatObjectInfo(pCallbackData);
    if (!objectInfo.empty()) {
        ss << objectInfo << " | ";
    }

    // Add message ID number
    ss << "MessageID = 0x" << std::hex << pCallbackData->messageIdNumber << " | ";

    // Add the main message
    ss << pCallbackData->pMessage;

    // Add label information
    std::string labelInfo = formatLabelInfo(pCallbackData);
    if (!labelInfo.empty()) {
        ss << labelInfo;
    }

    // Log the message with appropriate severity
    switch (messageSeverity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            INK_VERBOSE << ss.str();
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            INK_INFO << ss.str();
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            INK_WARN << ss.str();
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            INK_ERROR << ss.str();
            break;
        default:
            INK_WARN << "Unknown message severity: " << ss.str();
            break;
    }

    // For errors, provide more detailed debug information
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        // Add additional debug information here if needed
        // For example, you could log the current state of your application
        // or provide hints on how to fix common validation errors
    }

    return VK_FALSE;  // Tell Vulkan not to abort
}

VkDebugUtilsMessengerCreateInfoEXT VkDebugger::setupDebugMessenger()
{
    VkDebugUtilsMessengerCreateInfoEXT debugInfo = {};
    debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugInfo.pfnUserCallback = debugCallback;
    return debugInfo;
}

VkResult VkDebugger::createDebugUtilsMessengerEXT(VkDebugUtilsMessengerCreateInfoEXT* debugInfo, VkAllocationCallbacks* pAllocator) {
    PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(*_vkInstance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(*_vkInstance, debugInfo, pAllocator, &_debugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

VkDebugUtilsMessengerEXT* VkDebugger::getVkDebugMessenger()
{
    return &_debugMessenger;
}

} // namespace aura3d
