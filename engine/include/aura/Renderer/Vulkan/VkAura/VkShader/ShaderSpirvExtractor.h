#ifndef SHADERSPIRVEXTRACTOR_H
#define SHADERSPIRVEXTRACTOR_H

#pragma once

#include <string>
#include <vector>

namespace aura3d
{
namespace vk
{

class ShaderSpirvExtractor
{
  public:
    ShaderSpirvExtractor();
    ~ShaderSpirvExtractor();

    void readVertFile(const std::string &filename);
    void readFragFile(const std::string &filename);

    void clear();

    const std::vector<char> &getVertByteCode() const;
    const std::vector<char> &getFragByteCode() const;

  private:
    std::vector<char> _vertShaderCode;
    std::vector<char> _fragShaderCode;

    std::vector<char> _readFile(const std::string &filename);
};

} // namespace vk
} // namespace aura3d

#endif // SHADERSPIRVEXTRACTOR_H
