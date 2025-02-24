#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <memory.h>

// #define VK_USE_PLATFORM_WIN32_KHR
// #define GLFW_EXPOSE_NATIVE_WIN32

#include <plog/Log.h>
#include <plog/Initializers/ConsoleInitializer.h>
#include <plog/Formatters/TxtFormatter.h>
// #include <plog/Formatters/MessageOnlyFormatter.h>
#include <plog/Appenders/ColorConsoleAppender.h>
#include <thread>

#if defined(GLFW_INCLUDE_VULKAN)
#include <Runners/VkRunner.h>
#elif defined(GLFW_INCLUDE_OPENGL)
#include <Runners/GlRunner.h>
#endif

#ifdef NDEBUG
    const plog::Severity plogSeverity = plog::info;
#else
    const plog::Severity plogSeverity = plog::debug;
#endif

int main(int argc, char **argv)
{
    static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
    plog::init(plogSeverity, &consoleAppender);

#if defined(GLFW_INCLUDE_VULKAN)
    VkRunner vkRunner;
    vkRunner.run();
#elif defined(GLFW_INCLUDE_OPENGL)
    GlRunner glRunner;
    glRunner.run();
#endif

    return 0;
}
