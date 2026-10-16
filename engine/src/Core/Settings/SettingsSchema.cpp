#include "aura/Core/Settings/SettingsSchema.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <ranges>

#include <ink/EnhancedJson.h>

namespace aura3d
{
namespace
{

#ifdef NDEBUG
constexpr bool kDebugBuild = false;
#else
constexpr bool kDebugBuild = true;
#endif

#ifdef AURA_PROFILE_FRAME
constexpr bool kProfileFrameBuild = true;
#else
constexpr bool kProfileFrameBuild = false;
#endif

#define AURA_UNPACK(...) __VA_ARGS__

// One array of spellings per choice setting.
#define AURA_NO_CHOICES(...)
#define AURA_CHOICES_DEF(id, path, def, choices, doc)                                                                  \
    constexpr auto kChoices_##id = std::to_array<std::string_view>({AURA_UNPACK choices});
#define AURA_CHOICES_BUILD(id, path, expr, choices, note, doc)                                                         \
    constexpr auto kChoices_##id = std::to_array<std::string_view>({AURA_UNPACK choices});
AURA_SETTINGS(AURA_NO_CHOICES, AURA_NO_CHOICES, AURA_NO_CHOICES, AURA_NO_CHOICES, AURA_NO_CHOICES, AURA_CHOICES_DEF,
              AURA_CHOICES_BUILD)
#undef AURA_NO_CHOICES
#undef AURA_CHOICES_DEF
#undef AURA_CHOICES_BUILD

#define AURA_DESC_BOOL(id, path, def, doc) SettingDesc{path, SettingType::Bool, def, 0, 0.0, {}, 0.0, 0.0, {}, {}, doc},
#define AURA_DESC_BOOL_BUILD(id, path, expr, note, doc)                                                                \
    SettingDesc{path, SettingType::Bool, expr, 0, 0.0, {}, 0.0, 0.0, {}, note, doc},
#define AURA_DESC_INT(id, path, def, lo, hi, doc)                                                                      \
    SettingDesc{path, SettingType::Int, false, def, 0.0, {}, lo, hi, {}, {}, doc},
#define AURA_DESC_FLOAT(id, path, def, lo, hi, doc)                                                                    \
    SettingDesc{path, SettingType::Float, false, 0, def, {}, lo, hi, {}, {}, doc},
#define AURA_DESC_TEXT(id, path, def, doc)                                                                             \
    SettingDesc{path, SettingType::Text, false, 0, 0.0, def, 0.0, 0.0, {}, {}, doc},
#define AURA_DESC_CHOICE(id, path, def, choices, doc)                                                                  \
    SettingDesc{path, SettingType::Choice, false, 0, 0.0, def, 0.0, 0.0, kChoices_##id, {}, doc},
#define AURA_DESC_CHOICE_BUILD(id, path, expr, choices, note, doc)                                                     \
    SettingDesc{path, SettingType::Choice, false, 0, 0.0, expr, 0.0, 0.0, kChoices_##id, note, doc},

constexpr std::array<SettingDesc, kSettingCount> kSchema{AURA_SETTINGS(AURA_DESC_BOOL, AURA_DESC_BOOL_BUILD,
                                                                       AURA_DESC_INT, AURA_DESC_FLOAT, AURA_DESC_TEXT,
                                                                       AURA_DESC_CHOICE, AURA_DESC_CHOICE_BUILD)};

#undef AURA_DESC_BOOL
#undef AURA_DESC_BOOL_BUILD
#undef AURA_DESC_INT
#undef AURA_DESC_FLOAT
#undef AURA_DESC_TEXT
#undef AURA_DESC_CHOICE
#undef AURA_DESC_CHOICE_BUILD
#undef AURA_UNPACK

//! A default that its own setting would refuse is a mistake in the table, caught when it is written.
constexpr bool defaultsAreValid()
{
    for (const SettingDesc &desc : kSchema)
    {
        switch (desc.type)
        {
        case SettingType::Int:
            if (static_cast<f64>(desc.fallbackInt) < desc.minimum || static_cast<f64>(desc.fallbackInt) > desc.maximum)
                return false;
            break;
        case SettingType::Float:
            if (desc.fallbackFloat < desc.minimum || desc.fallbackFloat > desc.maximum)
                return false;
            break;
        case SettingType::Choice:
            if (std::ranges::find(desc.choices, desc.fallbackText) == desc.choices.end())
                return false;
            break;
        case SettingType::Bool:
        case SettingType::Text:
            break;
        }
    }
    return true;
}

static_assert(defaultsAreValid(), "a setting's default is outside its own range or choices");

[[nodiscard]] usize editDistance(std::string_view a, std::string_view b)
{
    std::vector<usize> previous(b.size() + 1);
    std::vector<usize> current(b.size() + 1);
    for (usize j = 0; j <= b.size(); ++j)
        previous[j] = j;

    for (usize i = 1; i <= a.size(); ++i)
    {
        current[0] = i;
        for (usize j = 1; j <= b.size(); ++j)
        {
            const usize substitution = previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, substitution});
        }
        std::swap(previous, current);
    }
    return previous[b.size()];
}

[[nodiscard]] std::string number(f64 value)
{
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%g", value);
    return text.data();
}

[[nodiscard]] std::string_view typeName(SettingType type) noexcept
{
    switch (type)
    {
    case SettingType::Bool:
        return "bool";
    case SettingType::Int:
        return "int";
    case SettingType::Float:
        return "float";
    case SettingType::Text:
        return "text";
    case SettingType::Choice:
        return "choice";
    }
    return "";
}

[[nodiscard]] std::string defaultText(const SettingDesc &desc)
{
    if (!desc.buildNote.empty())
        return std::string{desc.buildNote};

    switch (desc.type)
    {
    case SettingType::Bool:
        return desc.fallbackBool ? "true" : "false";
    case SettingType::Int:
        return std::to_string(desc.fallbackInt);
    case SettingType::Float:
        return number(desc.fallbackFloat);
    case SettingType::Text:
    case SettingType::Choice:
        return "\"" + std::string{desc.fallbackText} + "\"";
    }
    return {};
}

} // namespace

std::span<const SettingDesc> settingsSchema() noexcept
{
    return kSchema;
}

const SettingDesc *findSetting(std::string_view path) noexcept
{
    const auto found = std::ranges::find(kSchema, path, &SettingDesc::path);
    return found == kSchema.end() ? nullptr : &*found;
}

std::string_view closestMatch(std::string_view word, std::span<const std::string_view> candidates)
{
    std::string_view best;
    usize bestDistance = word.size();
    for (const std::string_view candidate : candidates)
    {
        const usize distance = editDistance(word, candidate);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = candidate;
        }
    }
    //! Far enough away and a suggestion is noise.
    return bestDistance <= std::max<usize>(2, word.size() / 3) ? best : std::string_view{};
}

std::string_view suggestSetting(std::string_view path)
{
    static const std::vector<std::string_view> paths = []
    {
        std::vector<std::string_view> all;
        all.reserve(kSchema.size());
        for (const SettingDesc &desc : kSchema)
            all.push_back(desc.path);
        return all;
    }();
    return closestMatch(path, paths);
}

SettingValue settingDefault(const SettingDesc &desc)
{
    switch (desc.type)
    {
    case SettingType::Bool:
        return desc.fallbackBool;
    case SettingType::Int:
        return desc.fallbackInt;
    case SettingType::Float:
        return desc.fallbackFloat;
    case SettingType::Text:
    case SettingType::Choice:
        return std::string{desc.fallbackText};
    }
    return false;
}

std::string settingsJsonSchema()
{
    nlohmann::json root = nlohmann::json::object();
    root["$schema"] = "http://json-schema.org/draft-07/schema#";
    root["title"] = "Aura3D settings";
    root["type"] = "object";
    root["additionalProperties"] = false;
    root["properties"] = nlohmann::json::object();
    root["properties"]["$schema"] = {{"type", "string"}};

    for (const SettingDesc &desc : kSchema)
    {
        nlohmann::json leaf = nlohmann::json::object();
        leaf["description"] = std::string{desc.doc};

        switch (desc.type)
        {
        case SettingType::Bool:
            leaf["type"] = "boolean";
            break;
        case SettingType::Int:
            leaf["type"] = "integer";
            leaf["minimum"] = static_cast<i64>(desc.minimum);
            leaf["maximum"] = static_cast<i64>(desc.maximum);
            break;
        case SettingType::Float:
            leaf["type"] = "number";
            leaf["minimum"] = desc.minimum;
            leaf["maximum"] = desc.maximum;
            break;
        case SettingType::Text:
            leaf["type"] = "string";
            break;
        case SettingType::Choice:
            leaf["type"] = "string";
            leaf["enum"] = nlohmann::json::array();
            for (const std::string_view choice : desc.choices)
                leaf["enum"].push_back(std::string{choice});
            break;
        }

        //! A default that depends on the build would be wrong for the other one.
        if (desc.buildNote.empty())
        {
            std::visit(
                [&leaf](const auto &value)
                {
                    leaf["default"] = value;
                },
                settingDefault(desc));
        }

        nlohmann::json *node = &root["properties"];
        const std::string path{desc.path};
        usize start = 0;
        while (true)
        {
            const usize dot = path.find('.', start);
            if (dot == std::string::npos)
            {
                (*node)[path.substr(start)] = std::move(leaf);
                break;
            }
            nlohmann::json &section = (*node)[path.substr(start, dot - start)];
            if (section.is_null())
            {
                section = nlohmann::json::object();
                section["type"] = "object";
                section["additionalProperties"] = false;
                section["properties"] = nlohmann::json::object();
            }
            node = &section["properties"];
            start = dot + 1;
        }
    }

    return root.dump(2) + "\n";
}

std::string settingsMarkdown()
{
    // First segment is the section; a path with no dot is the file's own.
    std::vector<std::string> order;
    std::map<std::string, std::vector<const SettingDesc *>> sections;
    for (const SettingDesc &desc : kSchema)
    {
        const usize dot = desc.path.find('.');
        std::string section{dot == std::string_view::npos ? std::string_view{"file"} : desc.path.substr(0, dot)};
        if (!sections.contains(section))
            order.push_back(section);
        sections[section].push_back(&desc);
    }

    std::string out;
    out += "<!-- Generated from AURA_SETTINGS by the update-settings-docs target. Do not edit. -->\n\n";
    for (const std::string &section : order)
    {
        out += "## `" + section + "`\n\n| Key | Type | Default | Notes |\n|---|---|---|---|\n";
        for (const SettingDesc *desc : sections[section])
        {
            const usize dot = desc->path.find('.');
            const std::string key{dot == std::string_view::npos ? desc->path : desc->path.substr(dot + 1)};

            std::string notes{desc->doc};
            if (desc->type == SettingType::Choice)
            {
                notes += " One of ";
                for (usize i = 0; i < desc->choices.size(); ++i)
                    notes += (i ? ", `" : "`") + std::string{desc->choices[i]} + "`";
                notes += ".";
            }
            else if ((desc->type == SettingType::Int || desc->type == SettingType::Float) &&
                     desc->minimum != desc->maximum)
            {
                notes += " " + number(desc->minimum) + " to " + number(desc->maximum) + ".";
            }

            out += "| `" + key + "` | ";
            out += typeName(desc->type);
            out += " | " + defaultText(*desc);
            out += " | " + notes;
            out += " |\n";
        }
        out += "\n";
    }
    return out;
}

} // namespace aura3d
