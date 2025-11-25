#include "GlShaderManager.h"

#include <fstream>
#include <sstream>

namespace aura3d {
namespace gl {

GlShaderManager::GlShaderManager()
{
    // Empty
}

std::string GlShaderManager::readShaderSource_GL(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Falha ao abrir o arquivo (OpenGL): " + filename);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    file.close();
    return buffer.str();
}

}
}
