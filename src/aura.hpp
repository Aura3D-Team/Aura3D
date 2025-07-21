#ifndef AURA_HPP
#define AURA_HPP

#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>

// GLM config import
#include <glm/gtc/matrix_transform.hpp>
#define GLM_FORCE_RADIANS

#define APPLICATION_NAME "Aura3D"
#define ENGINE_NAME "Aura3D Engine"
#define APPLICATION_VERSION VK_MAKE_API_VERSION(1, 0, 0)
#define ENGINE_VERSION VK_MAKE_API_VERSION(1, 0, 0)
#define API_VERSION VK_API_VERSION_1_3

#define DEFAULT_WINDOW_WIDTH 1280
#define DEFAULT_WINDOW_HEIGHT 720

#define MAX_FRAMES_IN_FLIGHT 2
#define MAX_ATTRIBUTE_DESCRIPTION 3
#define MAX_SHADER_MODULES 8
#define MAX_DESCRIPTOR_SETS 4
#define MAX_BINDING_COUNT 16

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using f32 = float;
using f64 = double;

// Template array definitions
template <typename T>
using VkFixedArray = std::array<T, MAX_FRAMES_IN_FLIGHT>;

template <typename T>
using AttributeDescriptionArray = std::array<T, MAX_ATTRIBUTE_DESCRIPTION>;

template <typename T>
using ShaderModuleArray = std::array<T, MAX_SHADER_MODULES>;

template <typename T>
using DescriptorSetArray = std::array<T, MAX_DESCRIPTOR_SETS>;

template <typename T>
using BindingArray = std::array<T, MAX_BINDING_COUNT>;

namespace aura3d {

// Some defs below to facilitate learning, but, they are not being used kk.
enum class ShaderType {
    VERTEX,
    FRAGMENT,
    GEOMETRY,
    COMPUTE,
    TESSELLATION_CONTROL,
    TESSELLATION_EVALUATION
};

inline VkShaderStageFlagBits shaderTypeToVkShaderStage(ShaderType type) {
    switch (type) {
    case ShaderType::VERTEX: return VK_SHADER_STAGE_VERTEX_BIT;
    case ShaderType::FRAGMENT: return VK_SHADER_STAGE_FRAGMENT_BIT;
    case ShaderType::GEOMETRY: return VK_SHADER_STAGE_GEOMETRY_BIT;
    case ShaderType::COMPUTE: return VK_SHADER_STAGE_COMPUTE_BIT;
    case ShaderType::TESSELLATION_CONTROL: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    case ShaderType::TESSELLATION_EVALUATION: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    // default: -1;
    }
}

enum class BufferType {
    VERTEX,
    INDEX,
    UNIFORM,
    STORAGE,
    TRANSFER_SRC,
    TRANSFER_DST
};

enum class ImageType {
    TEXTURE_2D,
    TEXTURE_3D,
    TEXTURE_CUBE,
    DEPTH,
    COLOR_ATTACHMENT,
    STORAGE
};

enum class RenderPassAttachmentType {
    COLOR,
    DEPTH,
    STENCIL,
    DEPTH_STENCIL
};

}

#endif // AURA_HPP
