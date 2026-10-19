#include "aura/Core/Settings/SettingsStore.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>

#include <ink/EnhancedJson.h>
#include <ink/Inkogger.h>

namespace aura3d
{
namespace
{

[[nodiscard]] std::string upper(std::string_view text)
{
    std::string folded(text);
    std::ranges::transform(folded, folded.begin(),
                           [](unsigned char c)
                           {
                               return static_cast<char>(std::toupper(c));
                           });
    return folded;
}

[[nodiscard]] std::string join(std::span<const std::string_view> words)
{
    std::string joined;
    for (const std::string_view word : words)
        joined += (joined.empty() ? "" : ", ") + std::string{word};
    return joined;
}

[[nodiscard]] u16 indexOf(const SettingDesc &desc) noexcept
{
    return static_cast<u16>(&desc - settingsSchema().data());
}

[[nodiscard]] std::vector<SettingValue> defaultValues()
{
    std::vector<SettingValue> values;
    values.reserve(kSettingCount);
    for (const SettingDesc &desc : settingsSchema())
        values.push_back(settingDefault(desc));
    return values;
}

/// Brings @p value to the setting's own type and checks it; @p why says what is wrong when it returns false.
[[nodiscard]] bool coerce(const SettingDesc &desc, SettingValue &value, std::string &why)
{
    switch (desc.type)
    {
    case SettingType::Bool:
        if (!std::holds_alternative<bool>(value))
        {
            why = "expects true or false";
            return false;
        }
        return true;

    case SettingType::Int:
    case SettingType::Float:
    {
        f64 number = 0.0;
        if (const auto *whole = std::get_if<i64>(&value))
        {
            number = static_cast<f64>(*whole);
        }
        else if (const auto *fraction = std::get_if<f64>(&value))
        {
            number = *fraction;
            if (desc.type == SettingType::Int && number != std::floor(number))
            {
                why = "expects a whole number";
                return false;
            }
        }
        else
        {
            why = "expects a number";
            return false;
        }

        if (!(number >= desc.minimum && number <= desc.maximum))
        {
            why = formatNumber(number) + " is outside " + formatNumber(desc.minimum) + " to " +
                  formatNumber(desc.maximum);
            return false;
        }

        if (desc.type == SettingType::Int)
            value = static_cast<i64>(number);
        else
            value = number;
        return true;
    }

    case SettingType::Text:
        if (!std::holds_alternative<std::string>(value))
        {
            why = "expects text";
            return false;
        }
        return true;

    case SettingType::Choice:
    {
        const auto *text = std::get_if<std::string>(&value);
        if (!text)
        {
            why = "expects one of " + join(desc.choices);
            return false;
        }

        const std::string folded = lowerAscii(*text);
        if (const auto match = std::ranges::find(desc.choices, folded); match != desc.choices.end())
        {
            value = std::string{*match};
            return true;
        }

        why = "'" + *text + "' is not one of " + join(desc.choices);
        if (const std::string_view near = closestMatch(folded, desc.choices); !near.empty())
            why += "; did you mean '" + std::string{near} + "'?";
        return false;
    }
    }
    return false;
}

[[nodiscard]] std::optional<SettingValue> parseText(const SettingDesc &desc, std::string_view text)
{
    switch (desc.type)
    {
    case SettingType::Bool:
    {
        const std::string word = lowerAscii(text);
        if (word == "true" || word == "1" || word == "yes" || word == "on")
            return true;
        if (word == "false" || word == "0" || word == "no" || word == "off")
            return false;
        return std::nullopt;
    }
    case SettingType::Int:
    {
        i64 number = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
        if (error != std::errc{} || end != text.data() + text.size())
            return std::nullopt;
        return number;
    }
    case SettingType::Float:
    {
        //! strtod, not from_chars: floating-point from_chars is missing from some of the standard libraries built for.
        const std::string owned(text);
        char *end = nullptr;
        errno = 0;
        const f64 number = std::strtod(owned.c_str(), &end);
        if (owned.empty() || end != owned.c_str() + owned.size() || errno == ERANGE)
            return std::nullopt;
        return number;
    }
    case SettingType::Text:
    case SettingType::Choice:
        return std::string{text};
    }
    return std::nullopt;
}

void collect(const nlohmann::json &node, const std::string &prefix, SettingsSink &sink)
{
    for (const auto &[key, child] : node.items())
    {
        std::string path = prefix;
        if (!prefix.empty())
            path += '.';
        path += key;

        if (child.is_object())
        {
            collect(child, path, sink);
        }
        else if (child.is_boolean())
        {
            sink.set(path, child.get<bool>());
        }
        else if (child.is_number_unsigned() && child.get<u64>() > static_cast<u64>(std::numeric_limits<i64>::max()))
        {
            sink.report(path + ": too large for a whole number");
        }
        else if (child.is_number_integer())
        {
            sink.set(path, child.get<i64>());
        }
        else if (child.is_number_float())
        {
            sink.set(path, child.get<f64>());
        }
        else if (child.is_string())
        {
            sink.set(path, child.get<std::string>());
        }
        else
        {
            sink.report(path + ": a value must be a boolean, number or string");
        }
    }
}

} // namespace

SettingsSink::SettingsSink(std::vector<SettingValue> &values, std::vector<std::string> &diagnostics,
                           std::string_view origin)
    : _values(values), _diagnostics(diagnostics), _origin(origin)
{
}

bool SettingsSink::set(std::string_view path, SettingValue value)
{
    const SettingDesc *desc = findSetting(path);
    if (!desc)
    {
        std::string message = _origin + ": " + std::string{path} + ": no such setting";
        if (const std::string_view near = suggestSetting(path); !near.empty())
            message += "; did you mean '" + std::string{near} + "'?";
        _diagnostics.push_back(std::move(message));
        return false;
    }

    std::string why;
    if (!coerce(*desc, value, why))
    {
        _diagnostics.push_back(_origin + ": " + std::string{path} + ": " + why);
        return false;
    }

    _values[indexOf(*desc)] = std::move(value);
    return true;
}

bool SettingsSink::setText(std::string_view path, std::string_view text)
{
    const SettingDesc *desc = findSetting(path);
    if (!desc)
        return set(path, std::string{text});

    std::optional<SettingValue> parsed = parseText(*desc, text);
    if (!parsed)
    {
        _diagnostics.push_back(_origin + ": " + std::string{path} + ": '" + std::string{text} + "' is not a valid " +
                               (desc->type == SettingType::Bool    ? "boolean"
                                : desc->type == SettingType::Int   ? "whole number"
                                : desc->type == SettingType::Float ? "number"
                                                                   : "value"));
        return false;
    }
    return set(path, std::move(*parsed));
}

void SettingsSink::report(const std::string &message)
{
    _diagnostics.push_back(_origin + ": " + message);
}

SettingsStore::SettingsStore() : _values(defaultValues()), _runtime(kSettingCount)
{
}

SettingsStore::~SettingsStore() = default;

void SettingsStore::setSources(std::vector<std::unique_ptr<ISettingsSource>> sources)
{
    _sources = std::move(sources);
    reload();
}

void SettingsStore::reload()
{
    _diagnostics.clear();

    std::vector<SettingValue> values = defaultValues();
    for (const std::unique_ptr<ISettingsSource> &source : _sources)
    {
        SettingsSink sink(values, _diagnostics, source->name());
        source->load(sink);
    }
    applyRuntime(values);

    for (const std::string &message : _diagnostics)
        INK_WARN << "Settings: " << message;

    const std::vector<SettingValue> before = std::move(_values);
    _values = std::move(values);
    notify(before);
}

void SettingsStore::applyRuntime(std::vector<SettingValue> &values) const
{
    for (usize i = 0; i < _runtime.size(); ++i)
    {
        if (const std::optional<SettingValue> &pinned = _runtime[i]; pinned.has_value())
            values[i] = *pinned;
    }
}

void SettingsStore::clearRuntime()
{
    std::ranges::fill(_runtime, std::nullopt);
    reload();
}

void SettingsStore::notify(const std::vector<SettingValue> &before)
{
    if (_handlers.empty())
        return;

    const auto handlers = _handlers;
    for (usize i = 0; i < _values.size(); ++i)
    {
        if (before[i] == _values[i])
            continue;
        for (const auto &[id, handler] : handlers)
            handler(settingsSchema()[i].path);
    }
}

bool SettingsStore::setValue(u16 index, SettingValue value)
{
    const SettingDesc &desc = settingsSchema()[index];

    std::string why;
    if (!coerce(desc, value, why))
    {
        INK_WARN << "Settings: " << desc.path << ": " << why;
        return false;
    }

    _runtime[index] = value;
    if (_values[index] == value)
        return true;

    _values[index] = std::move(value);
    for (const auto handlers = _handlers; const auto &[id, handler] : handlers)
        handler(desc.path);
    return true;
}

u32 SettingsStore::subscribe(ChangeHandler handler)
{
    const u32 id = _nextHandler++;
    _handlers.emplace_back(id, std::move(handler));
    return id;
}

void SettingsStore::unsubscribe(u32 id)
{
    std::erase_if(_handlers,
                  [id](const auto &entry)
                  {
                      return entry.first == id;
                  });
}

std::optional<SettingValue> SettingsStore::value(std::string_view path) const
{
    const SettingDesc *desc = findSetting(path);
    if (!desc)
        return std::nullopt;
    return _values[indexOf(*desc)];
}

template <> bool SettingsStore::get<bool>(Key<bool> key) const
{
    return std::get<bool>(_values[key.index]);
}

template <> int SettingsStore::get<int>(Key<int> key) const
{
    return static_cast<int>(std::get<i64>(_values[key.index]));
}

template <> double SettingsStore::get<double>(Key<double> key) const
{
    return std::get<f64>(_values[key.index]);
}

template <> std::string SettingsStore::get<std::string>(Key<std::string> key) const
{
    return std::get<std::string>(_values[key.index]);
}

template <> bool SettingsStore::set<bool>(Key<bool> key, bool value)
{
    return setValue(key.index, value);
}

template <> bool SettingsStore::set<int>(Key<int> key, int value)
{
    return setValue(key.index, static_cast<i64>(value));
}

template <> bool SettingsStore::set<double>(Key<double> key, double value)
{
    return setValue(key.index, value);
}

template <> bool SettingsStore::set<std::string>(Key<std::string> key, std::string value)
{
    return setValue(key.index, std::move(value));
}

MemorySource::MemorySource(std::string name, std::vector<std::pair<std::string, SettingValue>> values)
    : _name(std::move(name)), _values(std::move(values))
{
}

std::string MemorySource::name() const
{
    return _name;
}

void MemorySource::load(SettingsSink &sink)
{
    for (const auto &[path, value] : _values)
        sink.set(path, value);
}

JsonFileSource::JsonFileSource(std::string path, bool required) : _path(std::move(path)), _required(required)
{
}

std::string JsonFileSource::name() const
{
    return "'" + _path + "'";
}

void JsonFileSource::load(SettingsSink &sink)
{
    if (!std::ifstream{_path}.good())
    {
        if (_required)
            sink.report("no readable file; running on the layers beneath it");
        return;
    }

    //! loadFromFile() reports a malformed document as null rather than saying why.
    const ink::EnhancedJson document = ink::EnhancedJson::loadFromFile(_path);
    if (!document.is_object())
    {
        sink.report("not a JSON object; ignored");
        return;
    }

    //! The editor's link to the schema, not a setting.
    ink::EnhancedJson settings = document;
    settings.erase("$schema");
    collect(settings, {}, sink);
}

EnvironmentSource::EnvironmentSource(std::string prefix) : _prefix(std::move(prefix))
{
}

std::string EnvironmentSource::name() const
{
    return "environment";
}

void EnvironmentSource::load(SettingsSink &sink)
{
    for (const SettingDesc &desc : settingsSchema())
    {
        std::string variable = _prefix + upper(desc.path);
        for (usize dot = variable.find('.'); dot != std::string::npos; dot = variable.find('.', dot + 2))
            variable.replace(dot, 1, "__");

        if (const char *text = std::getenv(variable.c_str()))
            sink.setText(desc.path, text);
    }
}

CommandLineSource::CommandLineSource(std::vector<std::string> arguments) : _arguments(std::move(arguments))
{
}

std::string CommandLineSource::name() const
{
    return "command line";
}

void CommandLineSource::load(SettingsSink &sink)
{
    const auto apply = [&sink](std::string_view assignment)
    {
        const usize equals = assignment.find('=');
        if (equals == std::string_view::npos)
        {
            sink.report("--set " + std::string{assignment} + ": expected path=value");
            return;
        }
        sink.setText(assignment.substr(0, equals), assignment.substr(equals + 1));
    };

    for (usize i = 0; i < _arguments.size(); ++i)
    {
        const std::string_view argument = _arguments[i];
        if (argument == "--set")
        {
            if (i + 1 < _arguments.size())
                apply(_arguments[++i]);
            else
                sink.report("--set: expected path=value after it");
        }
        else if (argument.starts_with("--set="))
            apply(argument.substr(6));
    }
}

} // namespace aura3d
