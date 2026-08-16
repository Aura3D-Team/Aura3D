#ifndef MTLDEVICEMANAGER_H
#define MTLDEVICEMANAGER_H

#pragma once

#include <string>

#include "aura/Renderer/Metal/MtlAura/MtlAuraCore.h"

namespace aura3d {
namespace mtl {

/**
 * @class MtlDeviceManager
 *
 * @brief Owns the MTLDevice and the single command queue every frame submits
 *        through.
 *
 * The Metal counterpart of VkInstanceManager + VkDeviceManager + VkQueueManager
 * rolled into one, because Metal collapses all three: there is no instance to
 * create, no queue family to interrogate, and no extension list to negotiate.
 * @c MTLCreateSystemDefaultDevice picks the display's GPU, and a command queue
 * accepts render, compute and blit work alike.
 *
 * What is left worth managing is device *capability*: this class records the
 * limits the rest of the backend needs (see maxBufferLength()) so no other
 * manager has to reach for the device to ask.
 */
class MtlDeviceManager {
public:
    /**
     * @brief Selects the system default device and creates its command queue.
     *
     * @throws AuraException if no Metal device is present (a Mac too old for
     *         Metal, or a process with no GPU access at all), or if the queue
     *         cannot be created.
     */
    MtlDeviceManager();

    ~MtlDeviceManager();

    MtlDeviceManager(const MtlDeviceManager&) = delete;
    MtlDeviceManager& operator=(const MtlDeviceManager&) = delete;

    /**
     * @brief The device every resource in this backend is created against.
     *
     * Never null once the constructor returns.
     */
    [[nodiscard]] MTL::Device* getDevice() const noexcept { return _device.get(); }

    /**
     * @brief The queue every command buffer is taken from.
     *
     * One queue for the whole renderer, deliberately: commands submitted to a
     * single Metal queue complete in submission order, which is what lets a
     * texture uploaded during a frame be sampled by that same frame without an
     * explicit barrier. Never null once the constructor returns.
     */
    [[nodiscard]] MTL::CommandQueue* getCommandQueue() const noexcept { return _commandQueue.get(); }

    /**
     * @brief Human-readable device name, for logs.
     *
     * Never empty: falls back to a placeholder if the device reports no name.
     */
    [[nodiscard]] const std::string& getDeviceName() const noexcept { return _deviceName; }

    /**
     * @brief Largest buffer the device will allocate, in bytes.
     *
     * Checked before every buffer upload (MtlBufferManager) so an oversized
     * mesh is rejected with a diagnostic rather than handed to Metal, which
     * fails such an allocation by returning nil and logging nothing useful.
     */
    [[nodiscard]] NS::UInteger maxBufferLength() const noexcept { return _maxBufferLength; }

    /**
     * @brief Whether the device belongs to the Apple GPU families (Apple
     *        silicon Macs, iPhone, iPad) rather than the Intel/AMD Macs.
     *
     * Only used to decide the default storage mode for uploaded geometry:
     * Apple GPUs have unified memory, where a shared-storage buffer costs
     * nothing extra to read, while a discrete GPU wants its geometry in private
     * VRAM. See MtlBufferManager::preferredStorageMode().
     */
    [[nodiscard]] bool hasUnifiedMemory() const noexcept { return _unifiedMemory; }

private:
    NS::SharedPtr<MTL::Device> _device;
    NS::SharedPtr<MTL::CommandQueue> _commandQueue;

    std::string _deviceName;
    NS::UInteger _maxBufferLength = 0;
    bool _unifiedMemory = false;
};

} // namespace mtl
} // namespace aura3d

#endif // MTLDEVICEMANAGER_H
