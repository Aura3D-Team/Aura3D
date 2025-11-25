#ifndef COLOR_DEFINITIONS_H
#define COLOR_DEFINITIONS_H

#pragma once

#include <array>
#include <ink/ink_base.hpp>

namespace aura3d {
namespace colors {

// Integer-based color structure (0-255)
struct RGB {
    u8 r;
    u8 g;
    u8 b;

    // Constructor for easy initialization
    constexpr RGB(u8 red, u8 green, u8 blue) : r(red), g(green), b(blue) {}

    // Convert to u32 (ARGB format)
    u32 toUint32() const {
        return 0xFF000000 | (r << 16) | (g << 8) | b;
    }
};

// Float-based color structure for Vulkan/OpenGL (0.0f-1.0f)
struct RGBf {
    float r;
    float g;
    float b;

    // Constructor for easy initialization
    constexpr RGBf(float red, float green, float blue) : r(red), g(green), b(blue) {}

    // Convert from RGB to RGBf
    static RGBf fromRGB(const RGB& rgb) {
        return RGBf(rgb.r / 255.0f, rgb.g / 255.0f, rgb.b / 255.0f);
    }

    // Convert from RGBf to RGB
    RGB toRGB() const {
        return RGB(
            static_cast<u8>(r * 255.0f),
            static_cast<u8>(g * 255.0f),
            static_cast<u8>(b * 255.0f)
            );
    }
};

// Helper function to convert RGB to u32 (ARGB format with full alpha)
constexpr u32 RGB_TO_UINT32(u8 r, u8 g, u8 b) {
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

// Helper function to convert RGB values to float for Vulkan/OpenGL
constexpr RGBf RGB_TO_FLOAT(u8 r, u8 g, u8 b) {
    return RGBf(r / 255.0f, g / 255.0f, b / 255.0f);
}

// ===============================================
// Basic Colors
// ===============================================
// Primary Colors
const RGB RED(255, 0, 0);
const u32 RED_UINT32 = RGB_TO_UINT32(255, 0, 0);
const RGBf RED_F(1.0f, 0.0f, 0.0f);

const RGB GREEN(0, 255, 0);
const u32 GREEN_UINT32 = RGB_TO_UINT32(0, 255, 0);
const RGBf GREEN_F(0.0f, 1.0f, 0.0f);

const RGB BLUE(0, 0, 255);
const u32 BLUE_UINT32 = RGB_TO_UINT32(0, 0, 255);
const RGBf BLUE_F(0.0f, 0.0f, 1.0f);

// Secondary Colors
const RGB YELLOW(255, 255, 0);
const u32 YELLOW_UINT32 = RGB_TO_UINT32(255, 255, 0);
const RGBf YELLOW_F(1.0f, 1.0f, 0.0f);

const RGB CYAN(0, 255, 255);
const u32 CYAN_UINT32 = RGB_TO_UINT32(0, 255, 255);
const RGBf CYAN_F(0.0f, 1.0f, 1.0f);

const RGB MAGENTA(255, 0, 255);
const u32 MAGENTA_UINT32 = RGB_TO_UINT32(255, 0, 255);
const RGBf MAGENTA_F(1.0f, 0.0f, 1.0f);

// Grayscale
const RGB BLACK(0, 0, 0);
const u32 BLACK_UINT32 = RGB_TO_UINT32(0, 0, 0);
const RGBf BLACK_F(0.0f, 0.0f, 0.0f);

const RGB WHITE(255, 255, 255);
const u32 WHITE_UINT32 = RGB_TO_UINT32(255, 255, 255);
const RGBf WHITE_F(1.0f, 1.0f, 1.0f);

const RGB GRAY(128, 128, 128);
const u32 GRAY_UINT32 = RGB_TO_UINT32(128, 128, 128);
const RGBf GRAY_F(0.5f, 0.5f, 0.5f);

const RGB LIGHT_GRAY(192, 192, 192);
const u32 LIGHT_GRAY_UINT32 = RGB_TO_UINT32(192, 192, 192);
const RGBf LIGHT_GRAY_F(0.753f, 0.753f, 0.753f);

const RGB DARK_GRAY(64, 64, 64);
const u32 DARK_GRAY_UINT32 = RGB_TO_UINT32(64, 64, 64);
const RGBf DARK_GRAY_F(0.251f, 0.251f, 0.251f);

// ===============================================
// Web Standard Colors
// ===============================================
const RGB ALICE_BLUE(240, 248, 255);
const u32 ALICE_BLUE_UINT32 = RGB_TO_UINT32(240, 248, 255);
const RGBf ALICE_BLUE_F(0.941f, 0.973f, 1.0f);

const RGB ANTIQUE_WHITE(250, 235, 215);
const u32 ANTIQUE_WHITE_UINT32 = RGB_TO_UINT32(250, 235, 215);
const RGBf ANTIQUE_WHITE_F(0.980f, 0.922f, 0.843f);

const RGB AQUA(0, 255, 255);
const u32 AQUA_UINT32 = RGB_TO_UINT32(0, 255, 255);
const RGBf AQUA_F(0.0f, 1.0f, 1.0f);

const RGB AQUAMARINE(127, 255, 212);
const u32 AQUAMARINE_UINT32 = RGB_TO_UINT32(127, 255, 212);
const RGBf AQUAMARINE_F(0.498f, 1.0f, 0.831f);

const RGB AZURE(240, 255, 255);
const u32 AZURE_UINT32 = RGB_TO_UINT32(240, 255, 255);
const RGBf AZURE_F(0.941f, 1.0f, 1.0f);

const RGB BEIGE(245, 245, 220);
const u32 BEIGE_UINT32 = RGB_TO_UINT32(245, 245, 220);
const RGBf BEIGE_F(0.961f, 0.961f, 0.863f);

const RGB BISQUE(255, 228, 196);
const u32 BISQUE_UINT32 = RGB_TO_UINT32(255, 228, 196);
const RGBf BISQUE_F(1.0f, 0.894f, 0.769f);

const RGB BLANCHED_ALMOND(255, 235, 205);
const u32 BLANCHED_ALMOND_UINT32 = RGB_TO_UINT32(255, 235, 205);
const RGBf BLANCHED_ALMOND_F(1.0f, 0.922f, 0.804f);

const RGB BLUE_VIOLET(138, 43, 226);
const u32 BLUE_VIOLET_UINT32 = RGB_TO_UINT32(138, 43, 226);
const RGBf BLUE_VIOLET_F(0.541f, 0.169f, 0.886f);

const RGB BROWN(165, 42, 42);
const u32 BROWN_UINT32 = RGB_TO_UINT32(165, 42, 42);
const RGBf BROWN_F(0.647f, 0.165f, 0.165f);

const RGB BURLYWOOD(222, 184, 135);
const u32 BURLYWOOD_UINT32 = RGB_TO_UINT32(222, 184, 135);
const RGBf BURLYWOOD_F(0.871f, 0.722f, 0.529f);

const RGB CADET_BLUE(95, 158, 160);
const u32 CADET_BLUE_UINT32 = RGB_TO_UINT32(95, 158, 160);
const RGBf CADET_BLUE_F(0.373f, 0.620f, 0.627f);

const RGB CHARTREUSE(127, 255, 0);
const u32 CHARTREUSE_UINT32 = RGB_TO_UINT32(127, 255, 0);
const RGBf CHARTREUSE_F(0.498f, 1.0f, 0.0f);

const RGB CHOCOLATE(210, 105, 30);
const u32 CHOCOLATE_UINT32 = RGB_TO_UINT32(210, 105, 30);
const RGBf CHOCOLATE_F(0.824f, 0.412f, 0.118f);

const RGB CORAL(255, 127, 80);
const u32 CORAL_UINT32 = RGB_TO_UINT32(255, 127, 80);
const RGBf CORAL_F(1.0f, 0.498f, 0.314f);

const RGB CORNFLOWER_BLUE(100, 149, 237);
const u32 CORNFLOWER_BLUE_UINT32 = RGB_TO_UINT32(100, 149, 237);
const RGBf CORNFLOWER_BLUE_F(0.392f, 0.584f, 0.929f);

const RGB CORNSILK(255, 248, 220);
const u32 CORNSILK_UINT32 = RGB_TO_UINT32(255, 248, 220);
const RGBf CORNSILK_F(1.0f, 0.973f, 0.863f);

const RGB CRIMSON(220, 20, 60);
const u32 CRIMSON_UINT32 = RGB_TO_UINT32(220, 20, 60);
const RGBf CRIMSON_F(0.863f, 0.078f, 0.235f);

const RGB DARK_BLUE(0, 0, 139);
const u32 DARK_BLUE_UINT32 = RGB_TO_UINT32(0, 0, 139);
const RGBf DARK_BLUE_F(0.0f, 0.0f, 0.545f);

const RGB DARK_CYAN(0, 139, 139);
const u32 DARK_CYAN_UINT32 = RGB_TO_UINT32(0, 139, 139);
const RGBf DARK_CYAN_F(0.0f, 0.545f, 0.545f);

const RGB DARK_GOLDENROD(184, 134, 11);
const u32 DARK_GOLDENROD_UINT32 = RGB_TO_UINT32(184, 134, 11);
const RGBf DARK_GOLDENROD_F(0.722f, 0.525f, 0.043f);

const RGB DARK_GREEN(0, 100, 0);
const u32 DARK_GREEN_UINT32 = RGB_TO_UINT32(0, 100, 0);
const RGBf DARK_GREEN_F(0.0f, 0.392f, 0.0f);

const RGB DARK_KHAKI(189, 183, 107);
const u32 DARK_KHAKI_UINT32 = RGB_TO_UINT32(189, 183, 107);
const RGBf DARK_KHAKI_F(0.741f, 0.718f, 0.420f);

const RGB DARK_MAGENTA(139, 0, 139);
const u32 DARK_MAGENTA_UINT32 = RGB_TO_UINT32(139, 0, 139);
const RGBf DARK_MAGENTA_F(0.545f, 0.0f, 0.545f);

const RGB DARK_OLIVE_GREEN(85, 107, 47);
const u32 DARK_OLIVE_GREEN_UINT32 = RGB_TO_UINT32(85, 107, 47);
const RGBf DARK_OLIVE_GREEN_F(0.333f, 0.420f, 0.184f);

const RGB DARK_ORANGE(255, 140, 0);
const u32 DARK_ORANGE_UINT32 = RGB_TO_UINT32(255, 140, 0);
const RGBf DARK_ORANGE_F(1.0f, 0.549f, 0.0f);

const RGB DARK_ORCHID(153, 50, 204);
const u32 DARK_ORCHID_UINT32 = RGB_TO_UINT32(153, 50, 204);
const RGBf DARK_ORCHID_F(0.6f, 0.196f, 0.8f);

const RGB DARK_RED(139, 0, 0);
const u32 DARK_RED_UINT32 = RGB_TO_UINT32(139, 0, 0);
const RGBf DARK_RED_F(0.545f, 0.0f, 0.0f);

const RGB DARK_SALMON(233, 150, 122);
const u32 DARK_SALMON_UINT32 = RGB_TO_UINT32(233, 150, 122);
const RGBf DARK_SALMON_F(0.914f, 0.588f, 0.478f);

const RGB DARK_SEA_GREEN(143, 188, 143);
const u32 DARK_SEA_GREEN_UINT32 = RGB_TO_UINT32(143, 188, 143);
const RGBf DARK_SEA_GREEN_F(0.561f, 0.737f, 0.561f);

const RGB DARK_SLATE_BLUE(72, 61, 139);
const u32 DARK_SLATE_BLUE_UINT32 = RGB_TO_UINT32(72, 61, 139);
const RGBf DARK_SLATE_BLUE_F(0.282f, 0.239f, 0.545f);

const RGB DARK_SLATE_GRAY(47, 79, 79);
const u32 DARK_SLATE_GRAY_UINT32 = RGB_TO_UINT32(47, 79, 79);
const RGBf DARK_SLATE_GRAY_F(0.184f, 0.310f, 0.310f);

const RGB DARK_TURQUOISE(0, 206, 209);
const u32 DARK_TURQUOISE_UINT32 = RGB_TO_UINT32(0, 206, 209);
const RGBf DARK_TURQUOISE_F(0.0f, 0.808f, 0.820f);

const RGB DARK_VIOLET(148, 0, 211);
const u32 DARK_VIOLET_UINT32 = RGB_TO_UINT32(148, 0, 211);
const RGBf DARK_VIOLET_F(0.580f, 0.0f, 0.827f);

const RGB DEEP_PINK(255, 20, 147);
const u32 DEEP_PINK_UINT32 = RGB_TO_UINT32(255, 20, 147);
const RGBf DEEP_PINK_F(1.0f, 0.078f, 0.576f);

const RGB DEEP_SKY_BLUE(0, 191, 255);
const u32 DEEP_SKY_BLUE_UINT32 = RGB_TO_UINT32(0, 191, 255);
const RGBf DEEP_SKY_BLUE_F(0.0f, 0.749f, 1.0f);

const RGB DIM_GRAY(105, 105, 105);
const u32 DIM_GRAY_UINT32 = RGB_TO_UINT32(105, 105, 105);
const RGBf DIM_GRAY_F(0.412f, 0.412f, 0.412f);

const RGB DODGER_BLUE(30, 144, 255);
const u32 DODGER_BLUE_UINT32 = RGB_TO_UINT32(30, 144, 255);
const RGBf DODGER_BLUE_F(0.118f, 0.565f, 1.0f);

const RGB FIREBRICK(178, 34, 34);
const u32 FIREBRICK_UINT32 = RGB_TO_UINT32(178, 34, 34);
const RGBf FIREBRICK_F(0.698f, 0.133f, 0.133f);

const RGB FLORAL_WHITE(255, 250, 240);
const u32 FLORAL_WHITE_UINT32 = RGB_TO_UINT32(255, 250, 240);
const RGBf FLORAL_WHITE_F(1.0f, 0.980f, 0.941f);

const RGB FOREST_GREEN(34, 139, 34);
const u32 FOREST_GREEN_UINT32 = RGB_TO_UINT32(34, 139, 34);
const RGBf FOREST_GREEN_F(0.133f, 0.545f, 0.133f);

const RGB FUCHSIA(255, 0, 255);
const u32 FUCHSIA_UINT32 = RGB_TO_UINT32(255, 0, 255);
const RGBf FUCHSIA_F(1.0f, 0.0f, 1.0f);

const RGB GAINSBORO(220, 220, 220);
const u32 GAINSBORO_UINT32 = RGB_TO_UINT32(220, 220, 220);
const RGBf GAINSBORO_F(0.863f, 0.863f, 0.863f);

const RGB GHOST_WHITE(248, 248, 255);
const u32 GHOST_WHITE_UINT32 = RGB_TO_UINT32(248, 248, 255);
const RGBf GHOST_WHITE_F(0.973f, 0.973f, 1.0f);

const RGB GOLD(255, 215, 0);
const u32 GOLD_UINT32 = RGB_TO_UINT32(255, 215, 0);
const RGBf GOLD_F(1.0f, 0.843f, 0.0f);

const RGB GOLDENROD(218, 165, 32);
const u32 GOLDENROD_UINT32 = RGB_TO_UINT32(218, 165, 32);
const RGBf GOLDENROD_F(0.855f, 0.647f, 0.125f);

// Continue with all web colors...
// (For brevity, I'll skip to just include some key colors and
// continue with Material Design colors)

// ===============================================
// Material Design Colors
// ===============================================
const RGB MAT_RED_500(244, 67, 54);
const u32 MAT_RED_500_UINT32 = RGB_TO_UINT32(244, 67, 54);
const RGBf MAT_RED_500_F(0.957f, 0.263f, 0.212f);

const RGB MAT_PINK_500(233, 30, 99);
const u32 MAT_PINK_500_UINT32 = RGB_TO_UINT32(233, 30, 99);
const RGBf MAT_PINK_500_F(0.914f, 0.118f, 0.388f);

const RGB MAT_PURPLE_500(156, 39, 176);
const u32 MAT_PURPLE_500_UINT32 = RGB_TO_UINT32(156, 39, 176);
const RGBf MAT_PURPLE_500_F(0.612f, 0.153f, 0.690f);

const RGB MAT_DEEP_PURPLE_500(103, 58, 183);
const u32 MAT_DEEP_PURPLE_500_UINT32 = RGB_TO_UINT32(103, 58, 183);
const RGBf MAT_DEEP_PURPLE_500_F(0.404f, 0.227f, 0.718f);

const RGB MAT_INDIGO_500(63, 81, 181);
const u32 MAT_INDIGO_500_UINT32 = RGB_TO_UINT32(63, 81, 181);
const RGBf MAT_INDIGO_500_F(0.247f, 0.318f, 0.710f);

const RGB MAT_BLUE_500(33, 150, 243);
const u32 MAT_BLUE_500_UINT32 = RGB_TO_UINT32(33, 150, 243);
const RGBf MAT_BLUE_500_F(0.129f, 0.588f, 0.953f);

const RGB MAT_LIGHT_BLUE_500(3, 169, 244);
const u32 MAT_LIGHT_BLUE_500_UINT32 = RGB_TO_UINT32(3, 169, 244);
const RGBf MAT_LIGHT_BLUE_500_F(0.012f, 0.663f, 0.957f);

const RGB MAT_CYAN_500(0, 188, 212);
const u32 MAT_CYAN_500_UINT32 = RGB_TO_UINT32(0, 188, 212);
const RGBf MAT_CYAN_500_F(0.0f, 0.737f, 0.831f);

const RGB MAT_TEAL_500(0, 150, 136);
const u32 MAT_TEAL_500_UINT32 = RGB_TO_UINT32(0, 150, 136);
const RGBf MAT_TEAL_500_F(0.0f, 0.588f, 0.533f);

const RGB MAT_GREEN_500(76, 175, 80);
const u32 MAT_GREEN_500_UINT32 = RGB_TO_UINT32(76, 175, 80);
const RGBf MAT_GREEN_500_F(0.298f, 0.686f, 0.314f);

const RGB MAT_LIGHT_GREEN_500(139, 195, 74);
const u32 MAT_LIGHT_GREEN_500_UINT32 = RGB_TO_UINT32(139, 195, 74);
const RGBf MAT_LIGHT_GREEN_500_F(0.545f, 0.765f, 0.290f);

const RGB MAT_LIME_500(205, 220, 57);
const u32 MAT_LIME_500_UINT32 = RGB_TO_UINT32(205, 220, 57);
const RGBf MAT_LIME_500_F(0.804f, 0.863f, 0.224f);

const RGB MAT_YELLOW_500(255, 235, 59);
const u32 MAT_YELLOW_500_UINT32 = RGB_TO_UINT32(255, 235, 59);
const RGBf MAT_YELLOW_500_F(1.0f, 0.922f, 0.231f);

const RGB MAT_AMBER_500(255, 193, 7);
const u32 MAT_AMBER_500_UINT32 = RGB_TO_UINT32(255, 193, 7);
const RGBf MAT_AMBER_500_F(1.0f, 0.757f, 0.027f);

const RGB MAT_ORANGE_500(255, 152, 0);
const u32 MAT_ORANGE_500_UINT32 = RGB_TO_UINT32(255, 152, 0);
const RGBf MAT_ORANGE_500_F(1.0f, 0.596f, 0.0f);

const RGB MAT_DEEP_ORANGE_500(255, 87, 34);
const u32 MAT_DEEP_ORANGE_500_UINT32 = RGB_TO_UINT32(255, 87, 34);
const RGBf MAT_DEEP_ORANGE_500_F(1.0f, 0.341f, 0.133f);

const RGB MAT_BROWN_500(121, 85, 72);
const u32 MAT_BROWN_500_UINT32 = RGB_TO_UINT32(121, 85, 72);
const RGBf MAT_BROWN_500_F(0.475f, 0.333f, 0.282f);

const RGB MAT_GREY_500(158, 158, 158);
const u32 MAT_GREY_500_UINT32 = RGB_TO_UINT32(158, 158, 158);
const RGBf MAT_GREY_500_F(0.620f, 0.620f, 0.620f);

const RGB MAT_BLUE_GREY_500(96, 125, 139);
const u32 MAT_BLUE_GREY_500_UINT32 = RGB_TO_UINT32(96, 125, 139);
const RGBf MAT_BLUE_GREY_500_F(0.376f, 0.490f, 0.545f);

// ===============================================
// Additional helper functions for Vulkan/OpenGL
// ===============================================

// Helper function to convert RGB to vec3-style array for shader uniforms
inline std::array<f32, 3> toVec3(const RGBf& color) {
    return {color.r, color.g, color.b};
}

inline float* toVec3(const RGBf& color, f32* outArray) {
    outArray[0] = color.r;
    outArray[1] = color.g;
    outArray[2] = color.b;
    return outArray;
}

// Helper function to convert RGB to vec4-style array (with alpha) for shader uniforms
inline std::array<f32, 4> toVec4(const RGBf& color, f32 alpha) {
    return {color.r, color.g, color.b, alpha};
}

inline float* toVec4(const RGBf& color, f32 alpha, f32* outArray) {
    outArray[0] = color.r;
    outArray[1] = color.g;
    outArray[2] = color.b;
    outArray[3] = alpha;
    return outArray;
}

// Convert RGB to float array [r, g, b]
inline void RGBtoFloatArray(const RGB& color, f32* outArray) {
    outArray[0] = color.r / 255.0f;
    outArray[1] = color.g / 255.0f;
    outArray[2] = color.b / 255.0f;
}

// Convert RGB to float array [r, g, b, a]
inline void RGBtoFloatArrayWithAlpha(const RGB& color, float alpha, f32* outArray) {
    outArray[0] = color.r / 255.0f;
    outArray[1] = color.g / 255.0f;
    outArray[2] = color.b / 255.0f;
    outArray[3] = alpha;
}

} // namespace colors
} // namespace aura3d
#endif // COLOR_DEFINITIONS_H
