#ifndef GLRUNNER_H
#define GLRUNNER_H

#pragma once

#include <GlfwAura/GlfwWindowManager/GlfwWindowManager.h>

namespace aura3d {

class GlRunner
{
public:
    GlRunner(WindowDetails windowDetails);

    void run();

private:
    std::unique_ptr<aura3d::GlfwWindowManager> _glfwWindowManager;
};

}

#endif // GLRUNNER_H
