#ifndef SHADERSPIRVEXTRACTOR_H
#define SHADERSPIRVEXTRACTOR_H

#pragma once

#include <vector>
#include <string>

namespace aura3d {
namespace vk {

class ShaderSpirvExtractor
{
public:
    ShaderSpirvExtractor();
    ~ShaderSpirvExtractor();

    void readVertFile(const std::string& filename);
    void readFragFile(const std::string& filename);

    void clear();

    const std::vector<char>& getVertByteCode() const;
    const std::vector<char>& getFragByteCode() const;

private:
    std::vector<char> _vertShaderCode;
    std::vector<char> _fragShaderCode;

    std::vector<char> _readFile(const std::string& filename);
};

}
}

#endif // SHADERSPIRVEXTRACTOR_H
