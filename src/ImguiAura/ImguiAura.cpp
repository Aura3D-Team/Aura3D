#include "ImguiAura.h"

#include <vector>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>

#include "VkAura/VkException/VkException.h"

ImguiAura::ImguiAura(VkInstance vkInstance, VkDevice device, VkPhysicalDevice physicalDevice,
                     VkQueue graphicsQueue, VkRenderPass renderPass, GLFWwindow* window, uint32_t imageCount)
    : _vkGuiInitInfo()
{
    // ImGui initialization
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
        throw VkException("Failed to init GLFW Vulkan Imgui!");
    }

    _vkGuiInitInfo.Instance = vkInstance;
    _vkGuiInitInfo.PhysicalDevice = physicalDevice;
    _vkGuiInitInfo.Device = device;
    _vkGuiInitInfo.Queue = graphicsQueue;
    _vkGuiInitInfo.RenderPass = renderPass;
    _vkGuiInitInfo.PipelineCache = VK_NULL_HANDLE;
    // _vkGuiInitInfo.DescriptorPool = _descriptorPools.back(); TODO
    _vkGuiInitInfo.MinImageCount = imageCount;
    _vkGuiInitInfo.ImageCount = imageCount;
    _vkGuiInitInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    _vkGuiInitInfo.Allocator = nullptr;

    if (!ImGui_ImplVulkan_Init(&_vkGuiInitInfo)) {
        throw VkException("Failed to init Vulkan Imgui!");
    }
}

ImguiAura::~ImguiAura()
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}
