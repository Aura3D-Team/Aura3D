#include "aura/Renderer/OpenGL/GlAura/GlShaderManager/GlShaderManager.h"

#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace aura3d
{
namespace gl
{

GlShaderManager::GlShaderManager()
{
    // Empty
}

std::string GlShaderManager::readShaderSource_GL(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Falha ao abrir o arquivo (OpenGL): " + filename);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    file.close();
    return buffer.str();
}

namespace
{

GLuint compileStage(GLenum type, const char *source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        std::array<char, 512> log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error(std::string("GL shader compile failed: ") + log.data());
    }
    return shader;
}

} // namespace

GLuint GlShaderManager::createProgram(const char *vertexSource, const char *fragmentSource)
{
    const GLuint vertex = compileStage(GL_VERTEX_SHADER, vertexSource);
    GLuint fragment = 0;
    try
    {
        fragment = compileStage(GL_FRAGMENT_SHADER, fragmentSource);
    }
    catch (...)
    {
        glDeleteShader(vertex);
        throw;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        std::array<char, 512> log{};
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        glDeleteProgram(program);
        throw std::runtime_error(std::string("GL shader link failed: ") + log.data());
    }
    return program;
}

} // namespace gl
} // namespace aura3d
