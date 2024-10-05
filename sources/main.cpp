#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <memory.h>

#include <plog/Log.h>
#include <plog/Initializers/ConsoleInitializer.h>
#include <plog/Formatters/TxtFormatter.h>
// #include <plog/Formatters/MessageOnlyFormatter.h>
#include <plog/Appenders/ColorConsoleAppender.h>
#include "VkInstanceManager/VkInstanceManager.h"

#define WIDTH 1280
#define HEIGHT 720

int main(int argc, char **argv)
{
    static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
    plog::init(plog::debug, &consoleAppender);

    if (!glfwInit()) {
        PLOG_ERROR << "Failed to initialize GLFW";
        return -1;
    }

    if (!glfwVulkanSupported()) {
        PLOG_ERROR << "Vulkan not supported by GLFW";
        return -1;
    }

    PLOG_INFO << "Plataform: " << glfwGetPlatform();

    // Set required GLFW window hints for Vulkan, because we are not using opengl
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow *window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Window", nullptr, nullptr);
    if (!window) {
        PLOG_ERROR << "Failed to create GLFW window";
        glfwTerminate();
        return -1;
    }

    std::unique_ptr<VkInstanceManager> vkInstance = std::make_unique<VkInstanceManager>();

    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glfwSwapBuffers(window);

        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
