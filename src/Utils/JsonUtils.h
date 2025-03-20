#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#pragma once

#include <string>
#include <vector>
#include <optional>

#include "EnhancedJson.h"

namespace aura3d {

class JsonUtils {
public:
    // Factory methods
    static EnhancedJson array();
    static EnhancedJson object();
    static EnhancedJson meta_info();

    // File operations
    static EnhancedJson loadFromFile(const std::string& filePath);
    static bool saveToFile(const EnhancedJson& json, const std::string& filePath, bool pretty = true, int indent = 4);
    static EnhancedJson loadFromString(const std::string& jsonStr);

    // Serialization
    static std::string toString(const EnhancedJson& json, bool pretty = false, int indent = 4);
    static std::vector<uint8_t> toBinary(const EnhancedJson& json, const std::string& format = "cbor");
    static EnhancedJson fromBinary(const std::vector<uint8_t>& data, const std::string& format = "cbor");

    // Safe accessors
    template<typename T>
    static std::optional<T> getValue(const EnhancedJson& json, const std::string& key, const T& defaultValue = T());

    template<typename T>
    static std::optional<T> getValueAt(const EnhancedJson& json, size_t index, const T& defaultValue = T());

    template<typename T>
    static std::optional<T> getValueByPath(const EnhancedJson& json, const std::string& path, const T& defaultValue = T());

    // Type checking utilities
    static bool isArray(const EnhancedJson& json);
    static bool isObject(const EnhancedJson& json);
    static bool isNull(const EnhancedJson& json);
    static bool isNumber(const EnhancedJson& json);
    static bool isString(const EnhancedJson& json);
    static bool isBoolean(const EnhancedJson& json);

    // Array utilities
    static size_t size(const EnhancedJson& json);
    static bool hasKey(const EnhancedJson& json, const std::string& key);
    static std::vector<std::string> getKeys(const EnhancedJson& json);

    // Merge and patch utilities
    static EnhancedJson merge(const EnhancedJson& a, const EnhancedJson& b);
    static EnhancedJson diff(const EnhancedJson& source, const EnhancedJson& target);
    static EnhancedJson patch(const EnhancedJson& source, const EnhancedJson& patch);

    // Schema validation
    static bool validate(const EnhancedJson& json, const EnhancedJson& schema);

    // Debug utilities
    static std::string getTypeName(const EnhancedJson& json);
    // static bool compareJson(const EnhancedJson& a, const EnhancedJson& b, bool ignoreOrder = false); TODO
};

// Template implementation for getValue
template<typename T>
std::optional<T> JsonUtils::getValue(const EnhancedJson& json, const std::string& key, const T& defaultValue) {
    return json.get<T>(key, defaultValue);
}

// Template implementation for getValueAt
template<typename T>
std::optional<T> JsonUtils::getValueAt(const EnhancedJson& json, size_t index, const T& defaultValue) {
    return json.get<T>(index, defaultValue);
}

// Template implementation for getValueByPath
template<typename T>
std::optional<T> JsonUtils::getValueByPath(const EnhancedJson& json, const std::string& path, const T& defaultValue) {
    return json.getPath<T>(path, defaultValue);
}

} // namespace aura3d

#endif // JSON_UTILS_H
