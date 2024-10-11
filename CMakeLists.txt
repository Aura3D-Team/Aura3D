cmake_minimum_required(VERSION $ENV{CMAKE_VERSION})

project(Aura3D
    LANGUAGES CXX
    VERSION 1.0.0
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Vulkan REQUIRED)

find_library(GLFW_LIB libglfw3.a ${LIBRARY_PATH}/lib/libglfw3.a)

file(GLOB_RECURSE SRC_FILES src/*cpp)
add_executable(${PROJECT_NAME}
    sources/main.cpp
    ${SRC_FILES}
    headers/VkInstanceManager/VkInstanceManager.h sources/VkInstanceManager/VkInstanceManager.cpp sources/main.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/headers
    ${LIBRARY_PATH}/include
    ${VULKAN_SDK}/include
)
target_link_libraries(${PROJECT_NAME} PUBLIC
    ${GLFW_LIB}
    Vulkan::Vulkan
    OpenGL::GL
    Threads::Threads
    dl
)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
