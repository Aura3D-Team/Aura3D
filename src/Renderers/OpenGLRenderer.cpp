#include "Renderers/OpenGLRenderer.h"
#include <glad/glad.h>
#include <cstring> // For memset

#include <thread>
#include <chrono>

#include "AuraCore.h"
#include "GlAura/GlShaderManager/GlShaderManager.h"

namespace aura3d {
namespace gl {

OpenGLRenderer::OpenGLRenderer(const wma::WindowDetails& windowDetails)
    : Renderer(windowDetails), _shaderProgram(0), _VAO(0), _VBO(0), _EBO(0)
{
    INK_INFO << "Renderer - OPENGL";
}

OpenGLRenderer::~OpenGLRenderer()
{
    cleanup();
}

void OpenGLRenderer::initialize()
{
    if (_isInitialized) {
        return;
    }

    // Create window manager
    _windowManagerApi = wma::createWindowManager(
        wma::WindowBackend::SDL2,
        _windowDetails,
        wma::GraphicsAPI::OpenGL
    );

    _windowManagerApi->getKeyboardListener().addKeyAction(wma::Key::KEY_ESCAPE, wma::KeyAction{
        [this](){ cleanup(); },
        nullptr
    });

    _isInitialized = true;
}

void OpenGLRenderer::createWindow(const char* title)
{
    _windowManagerApi->createWindow(title);
}

void OpenGLRenderer::run()
{
    if (!_isInitialized) {
        initialize();
    }

    // Create window with OpenGL context
    createWindow(APPLICATION_NAME);

    // 1. Setup Shaders first (so we have a program)
    setupShaders();

    // 2. Create Buffers (VAO, VBO, EBO)
    createVertexBuffers();
    createUniformBuffers(); // Setup UBO
    createDefaultTexture(); // Setup simple texture

    wma::WindowFlags* windowFlags = _windowManagerApi->getWindowFlags();

    // Main render loop
    _windowManagerApi->process([&]() {
        // Clear the color buffer
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (windowFlags->resized)
        {
            while (windowFlags->resized)
            {
                windowFlags->resized = false;
                std::this_thread::sleep_for(std::chrono::duration<f64, std::milli>(100));
            }
            handleWindowChanges();
        }

        // Update Matrices (every frame or when camera moves)
        updateGlobalMatrices();

        // Bind the shader program
        glUseProgram(_shaderProgram);

        // Bind Texture (Required because shader multiplies texture * color)
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, _whiteTexture);
        // Set sampler to texture unit 0
        glUniform1i(glGetUniformLocation(_shaderProgram, "textureSampler"), 0);

        // Bind the VAO (containing VBO and EBO config)
        glBindVertexArray(_VAO);
        // Issue draw calls using Indices (EBO)
        // Drawing 6 indices (2 triangles = 1 rectangle)
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        // Unbind VAO (optional, but good practice)
        glBindVertexArray(0);
    });
}

void OpenGLRenderer::setupShaders()
{
    // --- VERTEX SHADER ---
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    std::string vertexShaderSourceStr = GlShaderManager::readShaderSource_GL("./shaders/vert/gl_shader2d.vert");
    const char* vertexShaderSource = vertexShaderSourceStr.c_str();

    glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
    glCompileShader(vertexShader);

    int success;
    char log[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if(!success)
    {
        glGetShaderInfoLog(vertexShader, sizeof(log), nullptr, log);
        INK_THROW("ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" + std::string(log));
    }

    // --- FRAGMENT SHADER ---
    unsigned int fragShader = glCreateShader(GL_FRAGMENT_SHADER);
    std::string fragShaderSourceStr = GlShaderManager::readShaderSource_GL("./shaders/frag/gl_shader2d.frag");
    // BUG FIX: You originally passed vertexShaderSourceStr here
    const char* fragShaderSource = fragShaderSourceStr.c_str();

    glShaderSource(fragShader, 1, &fragShaderSource, nullptr);
    glCompileShader(fragShader);

    glGetShaderiv(fragShader, GL_COMPILE_STATUS, &success);
    if(!success)
    {
        glGetShaderInfoLog(fragShader, sizeof(log), nullptr, log);
        INK_THROW("ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" + std::string(log));
    }

    // --- SHADER PROGRAM ---
    _shaderProgram = glCreateProgram();
    glAttachShader(_shaderProgram, vertexShader);
    glAttachShader(_shaderProgram, fragShader);
    glLinkProgram(_shaderProgram);

    glGetProgramiv(_shaderProgram, GL_LINK_STATUS, &success);
    if(!success)
    {
        glGetProgramInfoLog(_shaderProgram, sizeof(log), nullptr, log);
        INK_THROW("ERROR::SHADER::PROGRAM::LINKING_FAILED\n" + std::string(log));
    }

    // Clean up shaders as they are linked into the program now
    glDeleteShader(vertexShader);
    glDeleteShader(fragShader);
}

void OpenGLRenderer::createVertexBuffers()
{
    // Define vertices for a Rectangle (Unique vertices)
    const std::vector<Vertex2d> vertices = {
        {{-0.5f, -0.5f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}}, // Red
        {{ 0.5f, -0.5f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}}, // Green
        {{ 0.5f,  0.5f}, {0.5f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}}, // Blue
        {{-0.5f,  0.5f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}  // White
    };

    // Define indices for the EBO (2 Triangles making a Rectangle)
    unsigned int indices[] = {
        0, 1, 2,   // first triangle
        2, 3, 0    // second triangle
    };

    // 1. Generate all buffers
    glGenVertexArrays(1, &_VAO);
    glGenBuffers(1, &_VBO);
    glGenBuffers(1, &_EBO);

    // 2. Bind the VAO first!
    // Any subsequent VBO/EBO configurations are stored in this VAO.
    glBindVertexArray(_VAO);

    // 3. Copy vertex array in a buffer for OpenGL to use
    glBindBuffer(GL_ARRAY_BUFFER, _VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex2d), vertices.data(), GL_STATIC_DRAW);

    // 4. Copy index array in a element buffer
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // 3. ATTRIBUTE POINTERS (Matches Struct layout)
    // Location 0: Position (2 floats)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2d), (void*)offsetof(Vertex2d, pos));
    glEnableVertexAttribArray(0);

    // Location 1: TexCoord (2 floats)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2d), (void*)offsetof(Vertex2d, texCoord));
    glEnableVertexAttribArray(1);

    // Location 2: Color (4 floats)
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex2d), (void*)offsetof(Vertex2d, color));
    glEnableVertexAttribArray(2);

    // 6. Unbind the VAO so other calls don't accidentally modify it
    // Note: Do NOT unbind the EBO while the VAO is active,
    // because the EBO binding is part of the VAO state.
    glBindVertexArray(0);

    // You can unbind the VBO here, but strictly not necessary
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void OpenGLRenderer::createUniformBuffers()
{
    u32 ubo_size = 3 * sizeof(glm::mat4);

    glGenBuffers(1, &_UBO);
    glBindBuffer(GL_UNIFORM_BUFFER, _UBO);
    glBufferData(GL_UNIFORM_BUFFER, ubo_size, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // Link Shader Block to Binding Point 0
    u32 block_index = glGetUniformBlockIndex(_shaderProgram, "UniformBufferObject");
    if (block_index != GL_INVALID_INDEX) {
        glUniformBlockBinding(_shaderProgram, block_index, 0);
        // Bind Buffer to Binding Point 0
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, _UBO);
    } else {
        // Handle error: Block not found in shader (check shader spelling)
        INK_ERROR << "UniformBufferObject block not found in shader!";
    }
}

void OpenGLRenderer::updateGlobalMatrices()
{
    // Create Identity Matrices (No transformation)
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 proj = glm::mat4(1.0f);

    // to see perspective, uncomment this
    // proj = glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);
    // view = glm::translate(view, glm::vec3(0.0f, 0.0f, -3.0f));

    glBindBuffer(GL_UNIFORM_BUFFER, _UBO);

    // Layout std140 ensures we can map directly if careful,
    // but uploading sub-data is safer for alignment.
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4), glm::value_ptr(model));
    glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(view));
    glBufferSubData(GL_UNIFORM_BUFFER, 2 * sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(proj));

    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void OpenGLRenderer::createDefaultTexture()
{
    // Create a 1x1 White Texture so the gradient color is multiplied by White (1.0)
    glGenTextures(1, &_whiteTexture);
    glBindTexture(GL_TEXTURE_2D, _whiteTexture);

    unsigned char whitePixel[] = { 255, 255, 255, 255 };

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

void OpenGLRenderer::handleWindowChanges()
{
    const wma::WindowDetails* windowDetails = _windowManagerApi->getWindowDetails();
    glViewport(0, 0, windowDetails->width, windowDetails->height);
}

void OpenGLRenderer::cleanup()
{
    if (!_isInitialized) {
        return;
    }

    // Delete OpenGL resources
    glDeleteVertexArrays(1, &_VAO);
    glDeleteBuffers(1, &_VBO);
    glDeleteBuffers(1, &_EBO);
    glDeleteBuffers(1, &_UBO);
    glDeleteTextures(1, &_whiteTexture);
    glDeleteProgram(_shaderProgram);

    // Reset window system
    _windowManagerApi.reset();

    _isInitialized = false;
}

}
} // namespace aura3d
