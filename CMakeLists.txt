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

file(GLOB_RECURSE SOURCES
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h
)

add_executable(${PROJECT_NAME} ${SOURCES})

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message("Debug build detected")

    set(VK_LAYER_SETTINGS_FILE "${CMAKE_SOURCE_DIR}/vk_layer_settings.txt")
    set(VK_LAYER_DESTINATION "${CMAKE_BINARY_DIR}")

    message("Source file: ${VK_LAYER_SETTINGS_FILE}")
    message("Destination directory: ${VK_LAYER_DESTINATION}")

    add_custom_target(copy_vk_layer_settings ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VK_LAYER_DESTINATION}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VK_LAYER_SETTINGS_FILE}"
            "${VK_LAYER_DESTINATION}/vk_layer_settings.txt"
    )
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
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
