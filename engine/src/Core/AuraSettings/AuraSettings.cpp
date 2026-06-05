#include "aura/Core/AuraSettings/AuraSettings.h"

namespace aura3d {

AuraSettings* AuraSettings::get()
{
    static AuraSettings instance;
    return &instance;
}

ink::EnhancedJson* AuraSettings::getSettings()
{
    return &_settings;
}

}
