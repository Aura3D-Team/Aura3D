cmake_minimum_required(VERSION 3.5)

project(Aura3D LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories("/usr/local/include")

add_executable(Aura3D
    src/main.cpp
    src/VkInstanceManager/VkInstanceManager.cpp src/VkInstanceManager/VkInstanceManager.h
)

include(GNUInstallDirs)
install(TARGETS Aura3D
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
