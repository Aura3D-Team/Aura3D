#include "GlRunner.h"

#include <memory>

// #include <glad/glad.h>
#include <GlfwAura/GlfwWindowManager/GlfwWindowManager.h>

#include <plog/Log.h>

GlRunner::GlRunner() {}

void GlRunner::run()
{
    PLOG_DEBUG << "Hello GL";

    std::unique_ptr<aura3d::GlfwWindowManager> glfwWindowManager = std::make_unique<aura3d::GlfwWindowManager>(1280, 720);

    glfwWindowManager->process([&]() {

    });
}
