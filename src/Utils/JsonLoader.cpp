#include "JsonLoader.h"

#include <fstream>
#include <plog/Log.h>

namespace aura3d {

JsonLoader::JsonLoader()
{
    // Empty
}

JsonLoader::~JsonLoader()
{
    // Empty
}

nlohmann::json JsonLoader::array()
{
    return nlohmann::json::array();
}

nlohmann::json JsonLoader::object()
{
    return nlohmann::json::object();
}

nlohmann::json JsonLoader::meta_info()
{
    return nlohmann::json::meta();
}

nlohmann::json JsonLoader::loadJsonFromFile(const std::string& filePath)
{
    nlohmann::json jsonData;
    try {
        std::ifstream file(filePath);
        if (!file.is_open())
        {
            throw std::ios_base::failure("Failed to open the file: " + filePath);
        }

        file >> jsonData;

        PLOG_DEBUG << "Loaded JSON data in path " << filePath;
    }
    catch (const nlohmann::json::parse_error& e)
    {
        PLOG_ERROR << "JSON parse error in file (" << filePath << "): " << e.what();
    }
    catch (const std::exception& e)
    {
        PLOG_ERROR << "Error reading file (" << filePath << "): " << e.what();
    }

    // Return the loaded JSON data (empty in case of failure)
    return jsonData;
}

nlohmann::json JsonLoader::loadJsonFromString(const std::string& jDoc)
{
    nlohmann::json jsonData;
    try {
        jsonData = nlohmann::json::parse(jDoc);

        PLOG_DEBUG << "Loaded JSON data from string!";
    }
    catch (const nlohmann::json::parse_error& e)
    {
        PLOG_ERROR << "JSON parse error: " << e.what();
    }
    catch (const std::exception& e)
    {
        PLOG_ERROR << "Error: " << e.what();
    }

    // Return the loaded JSON data (empty in case of failure)
    return jsonData;
}

const std::string JsonLoader::jsonToCompactString(nlohmann::json& jObj)
{
    return jObj.dump();
}

const std::string JsonLoader::jsonToIndentedString(nlohmann::json& jObj)
{
    return jObj.dump(4);
}

}

