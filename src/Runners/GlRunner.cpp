#include "GlRunner.h"

#include <aura.hpp>
#include <memory>

// #include <glad/glad.h>
#include <GlfwAura/GlfwWindowManager/GlfwWindowManager.h>

#include <plog/Log.h>

GlRunner::GlRunner() {}

void GlRunner::run()
{
    std::unique_ptr<aura3d::GlfwWindowManager> glfwWindowManager = std::make_unique<aura3d::GlfwWindowManager>(1280, 720);
    glfwWindowManager->createGlfwWindowManager(APPLICATION_NAME);

    glfwWindowManager->process([&]() {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    });
}
