#ifndef VKLOADER_H
#define VKLOADER_H

#pragma once

#include <vulkan/vulkan.h>

/// On Linux libvulkan is opened at run time, not linked: a machine without a Vulkan loader loses this backend
/// (RendererFactory falls back to OpenGL, then software) instead of the program failing before main(). The entry
/// points are then plain pointers named like the functions, and the library is built with VK_NO_PROTOTYPES so a
/// call that is missing from VkSymbols.inc fails to link. Other platforms link Vulkan and these are no-ops.
#ifdef AURA_VULKAN_DYNAMIC_LOADER
#define AURA_VK_FN(level, name) extern PFN_##name name;
#include "aura/Renderer/Vulkan/VkAura/VkLoader/VkSymbols.inc"
#undef AURA_VK_FN
extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
#endif

namespace aura3d
{
namespace vk
{
namespace loader
{

/// Opens the system Vulkan loader once per process. False when it is not installed.
[[nodiscard]] bool load() noexcept;

/// Binds the instance-level entry points. Call right after vkCreateInstance, before any other Vulkan call.
void bindInstance(VkInstance instance) noexcept;

} // namespace loader
} // namespace vk
} // namespace aura3d

#endif // VKLOADER_H
