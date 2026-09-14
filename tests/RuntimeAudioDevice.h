#pragma once
#include <wma/wma.hpp>

/// A device with no thread and no scratch buffer: the test drives the
/// installed callback itself, so any allocation it counts belongs to the
/// engine's mixer and nothing else.
class RuntimeAudioDevice final : public wma::IAudioDevice {
public:
    wma::WmaCode open(const wma::AudioDeviceConfig& value) override { config = value; return wma::WmaCode::Ok; }
    void close() noexcept override { running = false; }
    wma::WmaCode start() override { running = true; return wma::WmaCode::Ok; }
    void stop() noexcept override { running = false; }
    bool isRunning() const noexcept override { return running; }
    void setMixCallback(wma::AudioMixCallback value) override { callback = std::move(value); }
    const wma::AudioDeviceConfig& getConfig() const noexcept override { return config; }
    wma::AudioBackend getBackendType() const noexcept override { return wma::AudioBackend::Null; }
    /// Whatever open() was given; the engine reads its rate and channel count.
    wma::AudioDeviceConfig config{};
    /// The engine's mixer. Tests call it directly with their own block.
    wma::AudioMixCallback callback;
    bool running = false;
};
