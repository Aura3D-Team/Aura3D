#include "JsonUtils.h"
#include <iostream>

namespace aura3d {

// Factory methods
EnhancedJson JsonUtils::array() {
    return EnhancedJson::createArray();
}

EnhancedJson JsonUtils::object() {
    return EnhancedJson::createObject();
}

EnhancedJson JsonUtils::meta_info() {
    return EnhancedJson(nlohmann::json::meta());
}

// File operations
EnhancedJson JsonUtils::loadFromFile(const std::string& filePath) {
    return EnhancedJson::loadFromFile(filePath);
}

bool JsonUtils::saveToFile(const EnhancedJson& json, const std::string& filePath, bool pretty, int indent) {
    return json.saveToFile(filePath, pretty, indent);
}

EnhancedJson JsonUtils::loadFromString(const std::string& jsonStr) {
    try {
        return EnhancedJson(nlohmann::json::parse(jsonStr));
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "[ERROR] JSON parse error in string: " << e.what() << std::endl;
        return EnhancedJson();
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Error parsing JSON string: " << e.what() << std::endl;
        return EnhancedJson();
    }
}

// Serialization
std::string JsonUtils::toString(const EnhancedJson& json, bool pretty, int indent) {
    return pretty ? json.toPrettyString(indent) : json.toCompactString();
}

std::vector<uint8_t> JsonUtils::toBinary(const EnhancedJson& json, const std::string& format) {
    try {
        if (format == "cbor") {
            return json.toCBOR();
        } else if (format == "msgpack") {
            return json.toMsgPack();
        } else if (format == "bson") {
            return json.toBSON();
        } else {
            std::cerr << "[ERROR] Unsupported binary format: " << format << std::endl;
            return {};
        }
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Error converting JSON to binary format: " << e.what() << std::endl;
        return {};
    }
}

EnhancedJson JsonUtils::fromBinary(const std::vector<uint8_t>& data, const std::string& format) {
    try {
        if (format == "cbor") {
            return EnhancedJson::fromCBOR(data);
        } else if (format == "msgpack") {
            return EnhancedJson::fromMsgPack(data);
        } else if (format == "bson") {
            return EnhancedJson::fromBSON(data);
        } else {
            std::cerr << "[ERROR] Unsupported binary format: " << format << std::endl;
            return EnhancedJson();
        }
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Error converting binary to JSON: " << e.what() << std::endl;
        return EnhancedJson();
    }
}

// Type checking utilities
bool JsonUtils::isArray(const EnhancedJson& json) {
    return json.is_array();
}

bool JsonUtils::isObject(const EnhancedJson& json) {
    return json.is_object();
}

bool JsonUtils::isNull(const EnhancedJson& json) {
    return json.is_null();
}

bool JsonUtils::isNumber(const EnhancedJson& json) {
    return json.is_number();
}

bool JsonUtils::isString(const EnhancedJson& json) {
    return json.is_string();
}

bool JsonUtils::isBoolean(const EnhancedJson& json) {
    return json.is_boolean();
}

// Array utilities
size_t JsonUtils::size(const EnhancedJson& json) {
    return json.size();
}

bool JsonUtils::hasKey(const EnhancedJson& json, const std::string& key) {
    return json.has(key);
}

std::vector<std::string> JsonUtils::getKeys(const EnhancedJson& json) {
    return json.keys();
}

// Merge and patch utilities
EnhancedJson JsonUtils::merge(const EnhancedJson& a, const EnhancedJson& b) {
    EnhancedJson result = a;
    result.merge(b);
    return result;
}

EnhancedJson JsonUtils::diff(const EnhancedJson& source, const EnhancedJson& target) {
    return EnhancedJson(nlohmann::json::diff(source, target));
}

EnhancedJson JsonUtils::patch(const EnhancedJson& source, const EnhancedJson& patchData) {
    return EnhancedJson(source.patch(patchData));
}

// Schema validation
bool JsonUtils::validate(const EnhancedJson& json, const EnhancedJson& schema) {
    return json.isValid(schema);
}

std::string JsonUtils::getTypeName(const EnhancedJson& json) {
    return json.type_name();
}

} // namespace aura3d
