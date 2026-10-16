/*
 * Writes the settings reference and JSON Schema from the schema in the engine: `gen_settings_docs <docs dir>`.
 * Run through the update-settings-docs target; test_settings fails when the committed copies are stale.
 */

#include <cstdio>
#include <fstream>
#include <string>

#include "aura/Core/Settings/SettingsSchema.h"

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::fputs("usage: gen_settings_docs <docs dir>\n", stderr);
        return 2;
    }

    const std::string directory = argv[1];
    std::ofstream{directory + "/aura.schema.json"} << aura3d::settingsJsonSchema();
    std::ofstream{directory + "/settings-reference.md"} << aura3d::settingsMarkdown();
    return 0;
}
