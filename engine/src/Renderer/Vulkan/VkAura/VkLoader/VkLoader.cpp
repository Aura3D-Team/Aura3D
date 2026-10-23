#include "aura/Renderer/Vulkan/VkAura/VkLoader/VkLoader.h"

#ifdef AURA_VULKAN_DYNAMIC_LOADER

#include <dlfcn.h>

namespace aura3d
{

#define AURA_VK_FN(level, name) PFN_##name name = nullptr;
#include "aura/Renderer/Vulkan/VkAura/VkLoader/VkSymbols.inc"
#undef AURA_VK_FN
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;

namespace vk
{
namespace loader
{

namespace
{

struct Binding
{
    const char *name;
    void **slot;
};

#define AURA_VK_BIND_Global(name) {#name, reinterpret_cast<void **>(&(name))},
#define AURA_VK_BIND_Instance(name)
#define AURA_VK_FN(level, name) AURA_VK_BIND_##level(name)
const Binding kGlobalBindings[] = {
#include "aura/Renderer/Vulkan/VkAura/VkLoader/VkSymbols.inc"
};
#undef AURA_VK_FN
#undef AURA_VK_BIND_Global
#undef AURA_VK_BIND_Instance

#define AURA_VK_BIND_Global(name)
#define AURA_VK_BIND_Instance(name) {#name, reinterpret_cast<void **>(&(name))},
#define AURA_VK_FN(level, name) AURA_VK_BIND_##level(name)
const Binding kInstanceBindings[] = {
#include "aura/Renderer/Vulkan/VkAura/VkLoader/VkSymbols.inc"
};
#undef AURA_VK_FN
#undef AURA_VK_BIND_Global
#undef AURA_VK_BIND_Instance

} // namespace

bool load() noexcept
{
    static const bool loaded = []
    {
        //! Never closed: drivers keep state that does not survive dlclose().
        void *library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!library)
            library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        if (!library)
            return false;

        vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library, "vkGetInstanceProcAddr"));
        if (!vkGetInstanceProcAddr)
            return false;

        for (const Binding &binding : kGlobalBindings)
        {
            *binding.slot = reinterpret_cast<void *>(vkGetInstanceProcAddr(VK_NULL_HANDLE, binding.name));
            if (!*binding.slot)
                return false;
        }
        return true;
    }();
    return loaded;
}

bool bindInstance(VkInstance instance) noexcept
{
    bool complete = true;
    for (const Binding &binding : kInstanceBindings)
    {
        *binding.slot = reinterpret_cast<void *>(vkGetInstanceProcAddr(instance, binding.name));
        complete = complete && *binding.slot != nullptr;
    }
    return complete;
}

} // namespace loader
} // namespace vk
} // namespace aura3d

#else

namespace aura3d
{
namespace vk
{
namespace loader
{

bool load() noexcept
{
    return true;
}

bool bindInstance(VkInstance) noexcept
{
    return true;
}

} // namespace loader
} // namespace vk
} // namespace aura3d

#endif
