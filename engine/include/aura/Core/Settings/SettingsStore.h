#ifndef AURA_SETTINGS_STORE_H
#define AURA_SETTINGS_STORE_H

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "aura/Core/Settings/SettingsSchema.h"

namespace aura3d
{

class SettingsStore;

/**
 * @brief Where a source writes. Everything it sets is checked against the schema first.
 *
 * A key the schema does not have, a value of the wrong type, one out of range or not among the choices is
 * reported, with the source's name and a suggestion where one is close, and leaves the earlier layers' value alone.
 */
class SettingsSink
{
  public:
    /// False when the value was refused.
    bool set(std::string_view path, SettingValue value);

    /// Reads @p text as the setting's own type: `true`, `800`, `0.5`, `fifo`.
    bool setText(std::string_view path, std::string_view text);

    /// A problem of the source's own, such as a file that is malformed.
    void report(const std::string &message);

  private:
    friend class SettingsStore;
    SettingsSink(std::vector<SettingValue> &values, std::vector<std::string> &diagnostics, std::string_view origin);

    std::vector<SettingValue> &_values;
    std::vector<std::string> &_diagnostics;
    std::string _origin;
};

/**
 * @brief A layer of settings: a file, the command line, the environment, code.
 *
 * The store runs its sources in order on every reload(), so a later one overrides an earlier one key by key.
 */
class ISettingsSource
{
  public:
    virtual ~ISettingsSource() = default;

    /// Named in diagnostics.
    [[nodiscard]] virtual std::string name() const = 0;

    virtual void load(SettingsSink &sink) = 0;
};

/**
 * @brief The effective value of every setting: defaults, then each source in turn, then runtime changes.
 *
 * Read once at startup by the subsystems that need a value, so nothing here is synchronised: load before threads
 * start, and call set() from the thread that owns the engine.
 */
class SettingsStore
{
  public:
    using ChangeHandler = std::function<void(std::string_view path)>;

    SettingsStore();
    SettingsStore(const SettingsStore &) = delete;
    SettingsStore &operator=(const SettingsStore &) = delete;
    ~SettingsStore();

    /// Replaces the sources and loads them. Runtime changes made with set() survive.
    void setSources(std::vector<std::unique_ptr<ISettingsSource>> sources);

    /// Runs the sources again, from the defaults up, and tells subscribers about what changed.
    void reload();

    /// The value of @p key. The explicit specialisations are bool, int, double and std::string.
    template <class T> [[nodiscard]] T get(Key<T> key) const;

    /// Changes a value for this run only, above every source; false when it was refused. Survives reload().
    template <class T> bool set(Key<T> key, std::type_identity_t<T> value);

    /// Drops runtime changes, back to what the sources say.
    void clearRuntime();

    /// Called after a reload or set() with the path of every setting whose value changed. Returns an id for
    /// unsubscribe().
    [[nodiscard]] u32 subscribe(ChangeHandler handler);
    void unsubscribe(u32 id);

    /// What the last reload() reported, one line each. Also logged.
    [[nodiscard]] const std::vector<std::string> &diagnostics() const noexcept
    {
        return _diagnostics;
    }

    /// The effective value by path, for tools and tests; empty for a path no setting has.
    [[nodiscard]] std::optional<SettingValue> value(std::string_view path) const;

  private:
    bool setValue(u16 index, SettingValue value);
    void applyRuntime(std::vector<SettingValue> &values) const;
    void notify(const std::vector<SettingValue> &before);

    std::vector<std::unique_ptr<ISettingsSource>> _sources;
    std::vector<SettingValue> _values;
    std::vector<std::optional<SettingValue>> _runtime;
    std::vector<std::string> _diagnostics;
    std::vector<std::pair<u32, ChangeHandler>> _handlers;
    u32 _nextHandler = 1;
};

template <> bool SettingsStore::get<bool>(Key<bool> key) const;
template <> int SettingsStore::get<int>(Key<int> key) const;
template <> double SettingsStore::get<double>(Key<double> key) const;
template <> std::string SettingsStore::get<std::string>(Key<std::string> key) const;

template <> bool SettingsStore::set<bool>(Key<bool> key, bool value);
template <> bool SettingsStore::set<int>(Key<int> key, int value);
template <> bool SettingsStore::set<double>(Key<double> key, double value);
template <> bool SettingsStore::set<std::string>(Key<std::string> key, std::string value);

/// A fixed set of values, the way code supplies them. Applied in the order given.
class MemorySource final : public ISettingsSource
{
  public:
    MemorySource(std::string name, std::vector<std::pair<std::string, SettingValue>> values);

    [[nodiscard]] std::string name() const override;
    void load(SettingsSink &sink) override;

  private:
    std::string _name;
    std::vector<std::pair<std::string, SettingValue>> _values;
};

/// A JSON document. Nested objects name a setting by their path, so `{"window": {"width": 800}}` is `window.width`.
class JsonFileSource final : public ISettingsSource
{
  public:
    /// A file that is absent is a report when @p required, and silent otherwise, as an optional platform file is.
    JsonFileSource(std::string path, bool required);

    [[nodiscard]] std::string name() const override;
    void load(SettingsSink &sink) override;

  private:
    std::string _path;
    bool _required;
};

/// `AURA_WINDOW__WIDTH=800` sets `window.width`: the path in capitals, each dot a double underscore.
class EnvironmentSource final : public ISettingsSource
{
  public:
    explicit EnvironmentSource(std::string prefix = "AURA_");

    [[nodiscard]] std::string name() const override;
    void load(SettingsSink &sink) override;

  private:
    std::string _prefix;
};

/// `--set window.width=800`, or `--set=window.width=800`, anywhere in the arguments. Others are left alone.
class CommandLineSource final : public ISettingsSource
{
  public:
    explicit CommandLineSource(std::vector<std::string> arguments);

    [[nodiscard]] std::string name() const override;
    void load(SettingsSink &sink) override;

  private:
    std::vector<std::string> _arguments;
};

} // namespace aura3d

#endif // AURA_SETTINGS_STORE_H
