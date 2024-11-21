#ifndef IMGUIAURA_H
#define IMGUIAURA_H

#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <imgui/backends/imgui_impl_vulkan.h>

class ImguiAura {
public:
    ImguiAura(VkInstance vkInstance, VkDevice device, VkPhysicalDevice physicalDevice,
              VkQueue graphicsQueue, VkRenderPass renderPass, GLFWwindow* window, uint32_t imageCount);
    ~ImguiAura();
private:
    ImGui_ImplVulkan_InitInfo _vkGuiInitInfo;
};

#endif // IMGUIAURA_H
