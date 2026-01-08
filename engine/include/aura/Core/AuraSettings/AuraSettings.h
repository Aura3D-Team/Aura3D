#ifndef AURASETTINGS_H
#define AURASETTINGS_H

#include <ink/EnhancedJson.h>

class AuraSettings {
public:
    static AuraSettings* get();

    ink::EnhancedJson* getSettings();

private:
    ink::EnhancedJson _settings;
};

#endif
