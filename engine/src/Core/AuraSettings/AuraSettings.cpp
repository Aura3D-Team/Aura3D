#include "aura/Core/AuraSettings/AuraSettings.h"

AuraSettings* AuraSettings::get()
{
    static AuraSettings instance;
    return &instance;
}

ink::EnhancedJson* AuraSettings::getSettings()
{
    return &_settings;
}
