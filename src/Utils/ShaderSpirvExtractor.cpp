#include "ShaderSpirvExtractor.h"

#include <fstream>

ShaderSpirvExtractor::ShaderSpirvExtractor() : _vertShaderCode(), _fragShaderCode() {}
ShaderSpirvExtractor::~ShaderSpirvExtractor() {
    _vertShaderCode.clear();
    _vertShaderCode.clear();
}

void ShaderSpirvExtractor::readVertFile(const std::string& filename)
{
    std::string pattern = "vert.spv";
    if (filename.size() < pattern.size() || filename.substr(filename.size() - pattern.size()) != pattern)
    {
        throw std::invalid_argument("Provided file does not have a .vert extension: " + filename);
    }
    _vertShaderCode = _readFile(filename);
}

void ShaderSpirvExtractor::readFragFile(const std::string& filename)
{
    std::string pattern = "frag.spv";
    if (filename.size() < pattern.size() || filename.substr(filename.size() - pattern.size()) != pattern)
    {
        throw std::invalid_argument("Provided file does not have a .vert extension: " + filename);
    }
    _fragShaderCode = _readFile(filename);
}

const std::vector<char>& ShaderSpirvExtractor::getVertByteCode() const
{
    return _vertShaderCode;
}

const std::vector<char>& ShaderSpirvExtractor::getFragByteCode() const
{
    return _fragShaderCode;
}

void ShaderSpirvExtractor::clear()
{
    _vertShaderCode.clear();
    _vertShaderCode.clear();
}

std::vector<char> ShaderSpirvExtractor::_readFile(const std::string& filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }

    std::size_t fileSize = (std::size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}
