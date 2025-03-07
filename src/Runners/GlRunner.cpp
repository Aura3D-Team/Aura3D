#include "GlRunner.h"

#include <aura.hpp>
#include <memory>

#include <plog/Log.h>

namespace aura3d {

GlRunner::GlRunner(WindowDetails windowDetails) {
    _glfwWindowManager = std::make_unique<aura3d::GlfwWindowManager>(windowDetails);
}

void GlRunner::run()
{
    _glfwWindowManager->createGlfwWindowManager(APPLICATION_NAME);

    _glfwWindowManager->process([&]() {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    });
}

}
