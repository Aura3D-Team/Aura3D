#ifndef VKLOADER_H
#define VKLOADER_H

#pragma once

#include <vulkan/vulkan.h>

/// On Linux libvulkan is opened at run time, not linked: a machine without a Vulkan loader loses this backend
/// (RendererFactory falls back to OpenGL, then software) instead of the program failing before main(). The entry
/// points are then pointers in namespace aura3d named like the functions, so engine code calls them unqualified and
/// a program that also links libvulkan keeps its own vkCreateInstance & co. The library is built with
/// VK_NO_PROTOTYPES, so a call missing from VkSymbols.inc fails to compile. Other platforms link Vulkan and the
/// loader functions are no-ops.
namespace aura3d
{

#ifdef AURA_VULKAN_DYNAMIC_LOADER
#define AURA_VK_FN(level, name) extern PFN_##name name;
#include "aura/Renderer/Vulkan/VkAura/VkLoader/VkSymbols.inc"
#undef AURA_VK_FN
extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
#endif

namespace vk
{
namespace loader
{

/// Opens the system Vulkan loader once per process. False when it is not installed.
[[nodiscard]] bool load() noexcept;

/// Binds the instance-level entry points. Call right after vkCreateInstance, before any other Vulkan call.
/// False when the loader does not provide one of them.
[[nodiscard]] bool bindInstance(VkInstance instance) noexcept;

} // namespace loader
} // namespace vk
} // namespace aura3d

#endif // VKLOADER_H
