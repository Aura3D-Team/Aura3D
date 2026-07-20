#include "aura/Core/AuraSettings/AuraSettings.h"

#include "aura/aura.h"

namespace aura3d {

namespace {

/*
 * The accessors are logically const: reading a key never changes the meaning
 * of the configuration. ink::EnhancedJson::getPath is reached through a
 * non-const reference because it is not const-qualified for the defaulting
 * overload. The referenced document is the function-local static owned by
 * AuraSettings::get(), which is never actually a const object, so this stays
 * well-defined.
 */
template <typename T>
T readPath(const ink::EnhancedJson& json, const char* path, T fallback)
{
    return const_cast<ink::EnhancedJson&>(json).getPath<T>(path, fallback);
}

} // namespace

AuraSettings* AuraSettings::get()
{
    static AuraSettings instance;
    return &instance;
}

ink::EnhancedJson* AuraSettings::getSettings()
{
    return &_settings;
}

void AuraSettings::reload(const std::string& path)
{
    _settings = ink::EnhancedJson::loadFromFile(path);
    INK_INFO << "AuraSettings: loaded configuration from " << path;
}

int AuraSettings::getWindowWidth() const
{
    return readPath<int>(_settings, "window/width", 1280);
}

int AuraSettings::getWindowHeight() const
{
    return readPath<int>(_settings, "window/height", 720);
}

std::string AuraSettings::getWindowTitle() const
{
    return readPath<std::string>(_settings, "window/title", APPLICATION_NAME);
}

bool AuraSettings::getWindowResizable() const
{
    return readPath<bool>(_settings, "window/resizable", true);
}

bool AuraSettings::getFullscreen() const
{
    return readPath<bool>(_settings, "window/fullscreen", false);
}

bool AuraSettings::getVSync() const
{
    return readPath<bool>(_settings, "window/vsync", false);
}

int AuraSettings::getFPSLimit() const
{
    return readPath<int>(_settings, "window/fps_limit", 60);
}

std::string AuraSettings::getRendererBackend() const
{
    return readPath<std::string>(_settings, "renderer/backend", "vulkan");
}

bool AuraSettings::getValidationLayers() const
{
#ifdef NDEBUG
    constexpr bool kDefault = false;
#else
    constexpr bool kDefault = true;
#endif
    return readPath<bool>(_settings, "renderer/validation_layers", kDefault);
}

int AuraSettings::getMaxFramesInFlight() const
{
    return readPath<int>(_settings, "renderer/max_frames_in_flight", 2);
}

std::string AuraSettings::getShadersPath() const
{
    return readPath<std::string>(_settings, "paths/shaders", "./resources/shaders/");
}

std::string AuraSettings::getTexturesPath() const
{
    return readPath<std::string>(_settings, "paths/textures", "./resources/textures/");
}

std::string AuraSettings::getModelsPath() const
{
    return readPath<std::string>(_settings, "paths/models", "./resources/models/");
}

} // namespace aura3d
