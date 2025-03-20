#ifndef ENHANCED_JSON_H
#define ENHANCED_JSON_H
#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>
#include <fstream>

namespace aura3d {

// Forward declaration for Query class
class JsonQuery;

/**
 * @brief Enhanced JSON class that extends nlohmann::json with additional functionality
 *
 * This class inherits from nlohmann::json and adds extra features:
 * - Safe accessors that don't throw exceptions
 * - Path-based access using dot notation
 * - Automatic type conversions
 * - Chained queries and filters
 * - Transformation and mapping functions
 * - Extended validation capabilities
 */
class EnhancedJson : public nlohmann::json {
public:
    // Constructors - inherit all constructors from the base class
    using nlohmann::json::json;

    // Additional constructors
    EnhancedJson() : nlohmann::json() {}

    // Copy constructor from nlohmann::json
    EnhancedJson(const nlohmann::json& other) : nlohmann::json(other) {}

    // Move constructor from nlohmann::json
    EnhancedJson(nlohmann::json&& other) noexcept : nlohmann::json(std::move(other)) {}

    // Operator overloads
    EnhancedJson& operator=(const nlohmann::json& other) {
        nlohmann::json::operator=(other);
        return *this;
    }

    EnhancedJson& operator=(nlohmann::json&& other) noexcept {
        nlohmann::json::operator=(std::move(other));
        return *this;
    }

    //
    // Safe accessors
    //

    /**
     * @brief Get value at specified key without throwing exception
     * @param key The key to look up
     * @param defaultValue Value to return if key is not found
     * @return Value at key or default value
     */
    template<typename T>
    T get(const std::string& key, const T& defaultValue = T()) const {
        try {
            if (is_object() && contains(key)) {
                return at(key).get<T>();
            }
        } catch (...) {
            // Swallow the exception and return default value
        }

        return defaultValue;
    }

    // Specialization for EnhancedJson
    EnhancedJson get(const std::string& key, const EnhancedJson& defaultValue = EnhancedJson()) const {
        try {
            if (is_object() && contains(key)) {
                return EnhancedJson(at(key));
            }
        } catch (...) {
            // Swallow the exception and return default value
        }

        return defaultValue;
    }

    /**
     * @brief Get value at array index without throwing exception
     * @param index The array index
     * @param defaultValue Value to return if index is out of bounds
     * @return Value at index or default value
     */
    template<typename T>
    T get(size_t index, const T& defaultValue = T()) const {
        try {
            if (is_array() && index < size()) {
                return at(index).get<T>();
            }
        } catch (...) {
            // Swallow the exception and return default value
        }

        return defaultValue;
    }

    // Specialization for EnhancedJson
    EnhancedJson get(size_t index, const EnhancedJson& defaultValue = EnhancedJson()) const {
        try {
            if (is_array() && index < size()) {
                return EnhancedJson(at(index));
            }
        } catch (...) {
            // Swallow the exception and return default value
        }

        return defaultValue;
    }

    /**
     * @brief Get value using dot notation path
     * @param path Path to the value (e.g., "user.address.street")
     * @param defaultValue Value to return if path is not found
     * @return Value at path or default value
     */
    template<typename T>
    T getPath(const std::string& path, const T& defaultValue = T()) const {
        try {
            size_t pos = path.find('.');

            // No more dots, this is the final segment
            if (pos == std::string::npos) {
                // Try to interpret as array index if it's a number
                try {
                    size_t index = std::stoul(path);
                    return get<T>(index, defaultValue);
                } catch (...) {
                    // Not a number, treat as object key
                    return get<T>(path, defaultValue);
                }
            }

            // Get the first segment
            std::string firstSegment = path.substr(0, pos);
            std::string remainingPath = path.substr(pos + 1);

            EnhancedJson nextJson;

            // Try to interpret as array index if it's a number
            try {
                size_t index = std::stoul(firstSegment);
                if (is_array() && index < size()) {
                    nextJson = EnhancedJson(at(index));
                } else {
                    return defaultValue;
                }
            } catch (...) {
                // Not a number, treat as object key
                if (is_object() && contains(firstSegment)) {
                    nextJson = EnhancedJson(at(firstSegment));
                } else {
                    return defaultValue;
                }
            }

            // Recursively get the value from the remaining path
            return nextJson.getPath<T>(remainingPath, defaultValue);
        } catch (...) {
            // Any exception means the path doesn't exist
            return defaultValue;
        }
    }

    // Specialization for EnhancedJson
    EnhancedJson getPath(const std::string& path, const EnhancedJson& defaultValue = EnhancedJson()) const {
        try {
            size_t pos = path.find('.');

            // No more dots, this is the final segment
            if (pos == std::string::npos) {
                // Try to interpret as array index if it's a number
                try {
                    size_t index = std::stoul(path);
                    return get(index, defaultValue);
                } catch (...) {
                    // Not a number, treat as object key
                    return get(path, defaultValue);
                }
            }

            // Get the first segment
            std::string firstSegment = path.substr(0, pos);
            std::string remainingPath = path.substr(pos + 1);

            EnhancedJson nextJson;

            // Try to interpret as array index if it's a number
            try {
                size_t index = std::stoul(firstSegment);
                if (is_array() && index < size()) {
                    nextJson = EnhancedJson(at(index));
                } else {
                    return defaultValue;
                }
            } catch (...) {
                // Not a number, treat as object key
                if (is_object() && contains(firstSegment)) {
                    nextJson = EnhancedJson(at(firstSegment));
                } else {
                    return defaultValue;
                }
            }

            // Recursively get the value from the remaining path
            return nextJson.getPath(remainingPath, defaultValue);
        } catch (...) {
            // Any exception means the path doesn't exist
            return defaultValue;
        }
    }

    /**
     * @brief Check if a key exists in the JSON object
     * @param key The key to check
     * @return True if key exists, false otherwise
     */
    bool has(const std::string& key) const {
        return is_object() && contains(key);
    }

    /**
     * @brief Check if a path exists in the JSON object
     * @param path The path to check (e.g., "user.address.street")
     * @return True if path exists, false otherwise
     */
    bool hasPath(const std::string& path) const {
        try {
            size_t pos = path.find('.');

            // No more dots, this is the final segment
            if (pos == std::string::npos) {
                // Try to interpret as array index if it's a number
                try {
                    size_t index = std::stoul(path);
                    return is_array() && index < size();
                } catch (...) {
                    // Not a number, treat as object key
                    return has(path);
                }
            }

            // Get the first segment
            std::string firstSegment = path.substr(0, pos);
            std::string remainingPath = path.substr(pos + 1);

            // Try to interpret as array index if it's a number
            try {
                size_t index = std::stoul(firstSegment);
                if (is_array() && index < size()) {
                    EnhancedJson nextJson = EnhancedJson(at(index));
                    return nextJson.hasPath(remainingPath);
                }
                return false;
            } catch (...) {
                // Not a number, treat as object key
                if (is_object() && contains(firstSegment)) {
                    EnhancedJson nextJson = EnhancedJson(at(firstSegment));
                    return nextJson.hasPath(remainingPath);
                }
                return false;
            }
        } catch (...) {
            return false;
        }
    }

    //
    // Array operations
    //

    /**
     * @brief Filter array elements based on a predicate
     * @param predicate Function that takes EnhancedJson and returns bool
     * @return New EnhancedJson array with filtered elements
     */
    EnhancedJson filter(std::function<bool(const EnhancedJson&)> predicate) const {
        EnhancedJson result = array();

        if (!is_array()) {
            return result;
        }

        for (const auto& item : *this) {
            EnhancedJson enhancedItem = EnhancedJson(item);
            if (predicate(enhancedItem)) {
                result.push_back(item);
            }
        }

        return result;
    }

    /**
     * @brief Map array elements using a transform function
     * @param transform Function that takes EnhancedJson and returns a transformed value
     * @return New EnhancedJson array with transformed elements
     */
    template<typename T>
    EnhancedJson map(std::function<T(const EnhancedJson&)> transform) const {
        EnhancedJson result = array();

        if (!is_array()) {
            return result;
        }

        for (const auto& item : *this) {
            EnhancedJson enhancedItem = EnhancedJson(item);
            result.push_back(transform(enhancedItem));
        }

        return result;
    }

    /**
     * @brief Reduce array elements to a single value
     * @param initial Initial value
     * @param reducer Function that accumulates values
     * @return Reduced value
     */
    template<typename T>
    T reduce(T initial, std::function<T(T, const EnhancedJson&)> reducer) const {
        T result = initial;

        if (!is_array()) {
            return result;
        }

        for (const auto& item : *this) {
            EnhancedJson enhancedItem = EnhancedJson(item);
            result = reducer(result, enhancedItem);
        }

        return result;
    }

    /**
     * @brief Find first element that matches a predicate
     * @param predicate Function that tests elements
     * @return First matching element or null
     */
    EnhancedJson find(std::function<bool(const EnhancedJson&)> predicate) const {
        if (!is_array()) {
            return EnhancedJson(nullptr);
        }

        for (const auto& item : *this) {
            EnhancedJson enhancedItem = EnhancedJson(item);
            if (predicate(enhancedItem)) {
                return enhancedItem;
            }
        }

        return EnhancedJson(nullptr);
    }

    /**
     * @brief Find all elements that match a predicate
     * @param predicate Function that tests elements
     * @return Array of matching elements
     */
    EnhancedJson findAll(std::function<bool(const EnhancedJson&)> predicate) const {
        return filter(predicate);
    }

    /**
     * @brief Check if any array element matches a predicate
     * @param predicate Function that tests elements
     * @return True if any element matches, false otherwise
     */
    bool any(std::function<bool(const EnhancedJson&)> predicate) const {
        if (!is_array()) {
            return false;
        }

        for (const auto& item : *this) {
            EnhancedJson enhancedItem = EnhancedJson(item);
            if (predicate(enhancedItem)) {
                return true;
            }
        }

        return false;
    }

    /**
     * @brief Check if all array elements match a predicate
     * @param predicate Function that tests elements
     * @return True if all elements match, false otherwise
     */
    bool all(std::function<bool(const EnhancedJson&)> predicate) const {
        if (!is_array() || empty()) {
            return false;
        }

        for (const auto& item : *this) {
            EnhancedJson enhancedItem = EnhancedJson(item);
            if (!predicate(enhancedItem)) {
                return false;
            }
        }

        return true;
    }

    //
    // Object operations
    //

    /**
     * @brief Set a value at the specified path, creating intermediate objects if needed
     * @param path Path to set the value at (e.g., "user.address.street")
     * @param value Value to set
     * @return Reference to this object
     */
    template<typename T>
    EnhancedJson& setPath(const std::string& path, const T& value) {
        size_t pos = path.find('.');

        // No more dots, this is the final segment
        if (pos == std::string::npos) {
            if (!is_object()) {
                *this = object();
            }
            (*this)[path] = value;
            return *this;
        }

        // Get the first segment and remaining path
        std::string firstSegment = path.substr(0, pos);
        std::string remainingPath = path.substr(pos + 1);

        // Ensure current object is an object
        if (!is_object()) {
            *this = object();
        }

        // Ensure the first segment exists and is an object
        if (!contains(firstSegment) || !at(firstSegment).is_object()) {
            (*this)[firstSegment] = object();
        }

        // Get the object at first segment and modify it
        EnhancedJson nextJson = EnhancedJson((*this)[firstSegment]);
        nextJson.setPath(remainingPath, value);
        (*this)[firstSegment] = nextJson;

        return *this;
    }

    /**
     * @brief Merge this JSON object with another
     * @param other JSON object to merge with
     * @param overwrite Whether to overwrite existing values
     * @return Reference to this object
     */
    EnhancedJson& merge(const EnhancedJson& other, bool overwrite = true) {
        if (!is_object() || !other.is_object()) {
            return *this;
        }

        for (auto it = other.begin(); it != other.end(); ++it) {
            const std::string& key = it.key();

            // If key exists in this object and both values are objects, recursively merge
            if (contains(key) && at(key).is_object() && it.value().is_object()) {
                EnhancedJson thisValue = EnhancedJson(at(key));
                EnhancedJson otherValue = EnhancedJson(it.value());
                thisValue.merge(otherValue, overwrite);
                (*this)[key] = thisValue;
            }
            // Otherwise, copy the value if it doesn't exist or overwrite is true
            else if (!contains(key) || overwrite) {
                (*this)[key] = it.value();
            }
        }

        return *this;
    }

    /**
     * @brief Get all keys of the JSON object
     * @return Vector of keys
     */
    std::vector<std::string> keys() const {
        std::vector<std::string> result;

        if (!is_object()) {
            return result;
        }

        for (auto it = begin(); it != end(); ++it) {
            result.push_back(it.key());
        }

        return result;
    }

    /**
     * @brief Remove a key from the JSON object
     * @param key Key to remove
     * @return True if key was removed, false otherwise
     */
    bool removeKey(const std::string& key) {
        if (!is_object() || !contains(key)) {
            return false;
        }

        erase(key);
        return true;
    }

    /**
     * @brief Remove a value at the specified path
     * @param path Path to the value to remove
     * @return True if value was removed, false otherwise
     */
    bool removePath(const std::string& path) {
        size_t pos = path.find('.');

        // No more dots, this is the final segment
        if (pos == std::string::npos) {
            return removeKey(path);
        }

        // Get the first segment and remaining path
        std::string firstSegment = path.substr(0, pos);
        std::string remainingPath = path.substr(pos + 1);

        // If the first segment exists and is an object, recursively remove from it
        if (is_object() && contains(firstSegment) && at(firstSegment).is_object()) {
            EnhancedJson nextJson = EnhancedJson((*this)[firstSegment]);
            bool result = nextJson.removePath(remainingPath);
            (*this)[firstSegment] = nextJson;
            return result;
        }

        return false;
    }

    //
    // Extended validation
    //

    /**
     * @brief Check if this JSON matches a schema
     * @param schema Schema to validate against
     * @return True if valid, false otherwise
     */
    bool isValid(const nlohmann::json& schema) const {
        // This is a simplified schema validation
        // For production, consider a dedicated JSON Schema validator library

        if (!schema.is_object()) {
            return false;
        }

        try {
            // Check type
            if (schema.contains("type")) {
                std::string type = schema["type"];

                if (type == "object" && !is_object()) return false;
                if (type == "array" && !is_array()) return false;
                if (type == "string" && !is_string()) return false;
                if (type == "number" && !is_number()) return false;
                if (type == "boolean" && !is_boolean()) return false;
                if (type == "null" && !is_null()) return false;
            }

            // Check properties for objects
            if (is_object() && schema.contains("properties") && schema["properties"].is_object()) {
                for (auto& [propName, propSchema] : schema["properties"].items()) {
                    if (contains(propName)) {
                        EnhancedJson propValue(at(propName));
                        if (!propValue.isValid(propSchema)) {
                            return false;
                        }
                    } else if (schema.contains("required") &&
                               schema["required"].is_array() &&
                               std::find(schema["required"].begin(), schema["required"].end(), propName) != schema["required"].end()) {
                        return false; // Missing required property
                    }
                }
            }

            // Check items for arrays
            if (is_array() && schema.contains("items") && schema["items"].is_object()) {
                for (const auto& item : *this) {
                    EnhancedJson itemValue(item);
                    if (!itemValue.isValid(schema["items"])) {
                        return false;
                    }
                }
            }
        } catch (...) {
            return false;
        }

        return true;
    }

    //
    // Query DSL - fluent interface for JSON queries
    //

    /**
     * @brief Start a query at the specified path
     * @param path Path to start query from
     * @return Query object for fluent interface
     */
    JsonQuery query(const std::string& path = "") const;

    //
    // Utility methods
    //

    /**
     * @brief Get a pretty-printed string representation
     * @param indent Number of spaces for indentation
     * @return Formatted JSON string
     */
    std::string toPrettyString(int indent = 4) const {
        return dump(indent);
    }

    /**
     * @brief Get a compact string representation
     * @return Compact JSON string
     */
    std::string toCompactString() const {
        return dump();
    }

    /**
     * @brief Convert to CBOR binary format
     * @return Vector of bytes
     */
    std::vector<uint8_t> toCBOR() const {
        return nlohmann::json::to_cbor(*this);
    }

    /**
     * @brief Convert to MessagePack binary format
     * @return Vector of bytes
     */
    std::vector<uint8_t> toMsgPack() const {
        return nlohmann::json::to_msgpack(*this);
    }

    /**
     * @brief Convert to BSON binary format
     * @return Vector of bytes
     */
    std::vector<uint8_t> toBSON() const {
        return nlohmann::json::to_bson(*this);
    }

    /**
     * @brief Create EnhancedJson from CBOR binary data
     * @param data CBOR binary data
     * @return EnhancedJson object
     */
    static EnhancedJson fromCBOR(const std::vector<uint8_t>& data) {
        return EnhancedJson(nlohmann::json::from_cbor(data));
    }

    /**
     * @brief Create EnhancedJson from MessagePack binary data
     * @param data MessagePack binary data
     * @return EnhancedJson object
     */
    static EnhancedJson fromMsgPack(const std::vector<uint8_t>& data) {
        return EnhancedJson(nlohmann::json::from_msgpack(data));
    }

    /**
     * @brief Create EnhancedJson from BSON binary data
     * @param data BSON binary data
     * @return EnhancedJson object
     */
    static EnhancedJson fromBSON(const std::vector<uint8_t>& data) {
        return EnhancedJson(nlohmann::json::from_bson(data));
    }

    /**
     * @brief Load EnhancedJson from a file
     * @param filepath Path to the JSON file
     * @return EnhancedJson object
     */
    static EnhancedJson loadFromFile(const std::string& filepath) {
        try {
            std::ifstream file(filepath);
            if (!file.is_open()) {
                return EnhancedJson();
            }

            EnhancedJson json;
            file >> json;
            return json;
        } catch (...) {
            return EnhancedJson();
        }
    }

    /**
     * @brief Save EnhancedJson to a file
     * @param filepath Path to save the JSON file
     * @param pretty Whether to pretty-print the JSON
     * @param indent Number of spaces for indentation
     * @return True if successful, false otherwise
     */
    bool saveToFile(const std::string& filepath, bool pretty = true, int indent = 4) const {
        try {
            std::ofstream file(filepath);
            if (!file.is_open()) {
                return false;
            }

            if (pretty) {
                file << dump(indent);
            } else {
                file << dump();
            }

            return true;
        } catch (...) {
            return false;
        }
    }

    //
    // Factory methods
    //

    /**
     * @brief Create an empty JSON array
     * @return EnhancedJson array
     */
    static EnhancedJson createArray() {
        return EnhancedJson(nlohmann::json::array());
    }

    /**
     * @brief Create an empty JSON object
     * @return EnhancedJson object
     */
    static EnhancedJson createObject() {
        return EnhancedJson(nlohmann::json::object());
    }
};

/**
 * @brief Query class for fluent interface to JSON data
 */
class JsonQuery {
public:
    JsonQuery(const EnhancedJson* root, const std::string& path = "")
        : m_root(root) {
        if (!path.empty()) {
            m_target = root->getPath(path);
        } else {
            m_target = *root;
        }
    }

    /**
     * @brief Get a value from the current target
     * @param key Key to get value for
     * @param defaultValue Default value if key doesn't exist
     * @return Value at key or default value
     */
    template<typename T>
    T get(const std::string& key, const T& defaultValue = T()) const {
        return m_target.get<T>(key, defaultValue);
    }

    /**
     * @brief Get a value from the current target at index
     * @param index Array index
     * @param defaultValue Default value if index is invalid
     * @return Value at index or default value
     */
    template<typename T>
    T get(size_t index, const T& defaultValue = T()) const {
        return m_target.get<T>(index, defaultValue);
    }

    /**
     * @brief Check if current target has a key
     * @param key Key to check
     * @return True if key exists, false otherwise
     */
    bool has(const std::string& key) const {
        return m_target.has(key);
    }

    /**
     * @brief Select a new path relative to current target
     * @param path Path to select
     * @return New query with updated target
     */
    JsonQuery select(const std::string& path) const {
        if (path.empty()) {
            return *this;
        }

        if (m_target.empty()) {
            return JsonQuery(m_root, path);
        }

        EnhancedJson newTarget = m_target.getPath(path);
        return JsonQuery(m_root, newTarget);
    }

    /**
     * @brief Filter the current target if it's an array
     * @param predicate Function to test elements
     * @return New query with filtered target
     */
    JsonQuery filter(std::function<bool(const EnhancedJson&)> predicate) const {
        EnhancedJson filtered = m_target.filter(predicate);
        return JsonQuery(m_root, filtered);
    }

    /**
     * @brief Get the first element of the current target if it's an array
     * @return New query with first element as target
     */
    JsonQuery first() const {
        if (m_target.is_array() && !m_target.empty()) {
            EnhancedJson firstElement = m_target[0];
            return JsonQuery(m_root, firstElement);
        }
        return JsonQuery(m_root, EnhancedJson());
    }

    /**
     * @brief Get the last element of the current target if it's an array
     * @return New query with last element as target
     */
    JsonQuery last() const {
        if (m_target.is_array() && !m_target.empty()) {
            EnhancedJson lastElement = m_target[m_target.size() - 1];
            return JsonQuery(m_root, lastElement);
        }
        return JsonQuery(m_root, EnhancedJson());
    }

    /**
     * @brief Get current query target
     * @return Current target
     */
    const EnhancedJson& value() const {
        return m_target;
    }

private:
    const EnhancedJson* m_root;
    EnhancedJson m_target;

    // Constructor for internal use
    JsonQuery(const EnhancedJson* root, const EnhancedJson& target)
        : m_root(root), m_target(target) {}
};

// Implementation of query method
inline JsonQuery EnhancedJson::query(const std::string& path) const {
    return JsonQuery(this, path);
}

} // namespace aura3d

#endif // ENHANCED_JSON_H
