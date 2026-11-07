#ifndef COLOR_DEFINITIONS_H
#define COLOR_DEFINITIONS_H

#pragma once

#include <array>
#include <ink/ink_base.hpp>

namespace aura3d
{
namespace colors
{

// Integer-based color structure (0-255)
struct RGB
{
    u8 r;
    u8 g;
    u8 b;

    // Constructor for easy initialization
    constexpr RGB(u8 red, u8 green, u8 blue) : r(red), g(green), b(blue)
    {
    }

    // Convert to u32 (ARGB format)
    u32 toUint32() const
    {
        return 0xFF000000 | (r << 16) | (g << 8) | b;
    }
};

// Float-based color structure for Vulkan/OpenGL (0.0f-1.0f)
struct RGBf
{
    float r;
    float g;
    float b;

    // Constructor for easy initialization
    constexpr RGBf(float red, float green, float blue) : r(red), g(green), b(blue)
    {
    }

    // Convert from RGB to RGBf
    static RGBf fromRGB(const RGB &rgb)
    {
        return RGBf(rgb.r / 255.0f, rgb.g / 255.0f, rgb.b / 255.0f);
    }

    // Convert from RGBf to RGB
    RGB toRGB() const
    {
        return RGB(static_cast<u8>(r * 255.0f), static_cast<u8>(g * 255.0f), static_cast<u8>(b * 255.0f));
    }
};

// Helper function to convert RGB to u32 (ARGB format with full alpha)
constexpr u32 RGB_TO_UINT32(u8 r, u8 g, u8 b)
{
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

// Helper function to convert RGB values to float for Vulkan/OpenGL
constexpr RGBf RGB_TO_FLOAT(u8 r, u8 g, u8 b)
{
    return RGBf(r / 255.0f, g / 255.0f, b / 255.0f);
}

//! Each colour in three forms: RGB, packed ARGB (NAME_UINT32) and normalised float (NAME_F), all from one byte triple.
#define AURA_COLOR(name, r, g, b)                                                                                      \
    inline constexpr RGB name(r, g, b);                                                                                \
    inline constexpr u32 name##_UINT32 = RGB_TO_UINT32(r, g, b);                                                       \
    inline constexpr RGBf name##_F = RGB_TO_FLOAT(r, g, b);

// ===============================================
// Basic Colors
// ===============================================
// Primary Colors
AURA_COLOR(RED, 255, 0, 0)
AURA_COLOR(GREEN, 0, 255, 0)
AURA_COLOR(BLUE, 0, 0, 255)

// Secondary Colors
AURA_COLOR(YELLOW, 255, 255, 0)
AURA_COLOR(CYAN, 0, 255, 255)
AURA_COLOR(MAGENTA, 255, 0, 255)

// Grayscale
AURA_COLOR(BLACK, 0, 0, 0)
AURA_COLOR(WHITE, 255, 255, 255)
AURA_COLOR(GRAY, 128, 128, 128)
AURA_COLOR(LIGHT_GRAY, 192, 192, 192)
AURA_COLOR(DARK_GRAY, 64, 64, 64)

// ===============================================
// Web Standard Colors
// ===============================================
AURA_COLOR(ALICE_BLUE, 240, 248, 255)
AURA_COLOR(ANTIQUE_WHITE, 250, 235, 215)
AURA_COLOR(AQUA, 0, 255, 255)
AURA_COLOR(AQUAMARINE, 127, 255, 212)
AURA_COLOR(AZURE, 240, 255, 255)
AURA_COLOR(BEIGE, 245, 245, 220)
AURA_COLOR(BISQUE, 255, 228, 196)
AURA_COLOR(BLANCHED_ALMOND, 255, 235, 205)
AURA_COLOR(BLUE_VIOLET, 138, 43, 226)
AURA_COLOR(BROWN, 165, 42, 42)
AURA_COLOR(BURLYWOOD, 222, 184, 135)
AURA_COLOR(CADET_BLUE, 95, 158, 160)
AURA_COLOR(CHARTREUSE, 127, 255, 0)
AURA_COLOR(CHOCOLATE, 210, 105, 30)
AURA_COLOR(CORAL, 255, 127, 80)
AURA_COLOR(CORNFLOWER_BLUE, 100, 149, 237)
AURA_COLOR(CORNSILK, 255, 248, 220)
AURA_COLOR(CRIMSON, 220, 20, 60)
AURA_COLOR(DARK_BLUE, 0, 0, 139)
AURA_COLOR(DARK_CYAN, 0, 139, 139)
AURA_COLOR(DARK_GOLDENROD, 184, 134, 11)
AURA_COLOR(DARK_GREEN, 0, 100, 0)
AURA_COLOR(DARK_KHAKI, 189, 183, 107)
AURA_COLOR(DARK_MAGENTA, 139, 0, 139)
AURA_COLOR(DARK_OLIVE_GREEN, 85, 107, 47)
AURA_COLOR(DARK_ORANGE, 255, 140, 0)
AURA_COLOR(DARK_ORCHID, 153, 50, 204)
AURA_COLOR(DARK_RED, 139, 0, 0)
AURA_COLOR(DARK_SALMON, 233, 150, 122)
AURA_COLOR(DARK_SEA_GREEN, 143, 188, 143)
AURA_COLOR(DARK_SLATE_BLUE, 72, 61, 139)
AURA_COLOR(DARK_SLATE_GRAY, 47, 79, 79)
AURA_COLOR(DARK_TURQUOISE, 0, 206, 209)
AURA_COLOR(DARK_VIOLET, 148, 0, 211)
AURA_COLOR(DEEP_PINK, 255, 20, 147)
AURA_COLOR(DEEP_SKY_BLUE, 0, 191, 255)
AURA_COLOR(DIM_GRAY, 105, 105, 105)
AURA_COLOR(DODGER_BLUE, 30, 144, 255)
AURA_COLOR(FIREBRICK, 178, 34, 34)
AURA_COLOR(FLORAL_WHITE, 255, 250, 240)
AURA_COLOR(FOREST_GREEN, 34, 139, 34)
AURA_COLOR(FUCHSIA, 255, 0, 255)
AURA_COLOR(GAINSBORO, 220, 220, 220)
AURA_COLOR(GHOST_WHITE, 248, 248, 255)
AURA_COLOR(GOLD, 255, 215, 0)
AURA_COLOR(GOLDENROD, 218, 165, 32)

//! CSS 'green' is (0, 128, 0); the GREEN above is the pure-channel primary
//! (0, 255, 0), which CSS calls 'lime'. Both are kept under distinct names.
AURA_COLOR(WEB_GREEN, 0, 128, 0)
AURA_COLOR(GREEN_YELLOW, 173, 255, 47)
AURA_COLOR(HONEYDEW, 240, 255, 240)
AURA_COLOR(HOT_PINK, 255, 105, 180)
AURA_COLOR(INDIAN_RED, 205, 92, 92)
AURA_COLOR(INDIGO, 75, 0, 130)
AURA_COLOR(IVORY, 255, 255, 240)
AURA_COLOR(KHAKI, 240, 230, 140)
AURA_COLOR(LAVENDER, 230, 230, 250)
AURA_COLOR(LAVENDER_BLUSH, 255, 240, 245)
AURA_COLOR(LAWN_GREEN, 124, 252, 0)
AURA_COLOR(LEMON_CHIFFON, 255, 250, 205)
AURA_COLOR(LIGHT_BLUE, 173, 216, 230)
AURA_COLOR(LIGHT_CORAL, 240, 128, 128)
AURA_COLOR(LIGHT_CYAN, 224, 255, 255)
AURA_COLOR(LIGHT_GOLDENROD_YELLOW, 250, 250, 210)
AURA_COLOR(LIGHT_GREEN, 144, 238, 144)
AURA_COLOR(LIGHT_PINK, 255, 182, 193)
AURA_COLOR(LIGHT_SALMON, 255, 160, 122)
AURA_COLOR(LIGHT_SEA_GREEN, 32, 178, 170)
AURA_COLOR(LIGHT_SKY_BLUE, 135, 206, 250)
AURA_COLOR(LIGHT_SLATE_GRAY, 119, 136, 153)
AURA_COLOR(LIGHT_STEEL_BLUE, 176, 196, 222)
AURA_COLOR(LIGHT_YELLOW, 255, 255, 224)
AURA_COLOR(LIME_GREEN, 50, 205, 50)
AURA_COLOR(LINEN, 250, 240, 230)
AURA_COLOR(MAROON, 128, 0, 0)
AURA_COLOR(MEDIUM_AQUAMARINE, 102, 205, 170)
AURA_COLOR(MEDIUM_BLUE, 0, 0, 205)
AURA_COLOR(MEDIUM_ORCHID, 186, 85, 211)
AURA_COLOR(MEDIUM_PURPLE, 147, 112, 219)
AURA_COLOR(MEDIUM_SEA_GREEN, 60, 179, 113)
AURA_COLOR(MEDIUM_SLATE_BLUE, 123, 104, 238)
AURA_COLOR(MEDIUM_SPRING_GREEN, 0, 250, 154)
AURA_COLOR(MEDIUM_TURQUOISE, 72, 209, 204)
AURA_COLOR(MEDIUM_VIOLET_RED, 199, 21, 133)
AURA_COLOR(MIDNIGHT_BLUE, 25, 25, 112)
AURA_COLOR(MINT_CREAM, 245, 255, 250)
AURA_COLOR(MISTY_ROSE, 255, 228, 225)
AURA_COLOR(MOCCASIN, 255, 228, 181)
AURA_COLOR(NAVAJO_WHITE, 255, 222, 173)
AURA_COLOR(NAVY, 0, 0, 128)
AURA_COLOR(OLD_LACE, 253, 245, 230)
AURA_COLOR(OLIVE, 128, 128, 0)
AURA_COLOR(OLIVE_DRAB, 107, 142, 35)
AURA_COLOR(ORANGE, 255, 165, 0)
AURA_COLOR(ORANGE_RED, 255, 69, 0)
AURA_COLOR(ORCHID, 218, 112, 214)
AURA_COLOR(PALE_GOLDENROD, 238, 232, 170)
AURA_COLOR(PALE_GREEN, 152, 251, 152)
AURA_COLOR(PALE_TURQUOISE, 175, 238, 238)
AURA_COLOR(PALE_VIOLET_RED, 219, 112, 147)
AURA_COLOR(PAPAYA_WHIP, 255, 239, 213)
AURA_COLOR(PEACH_PUFF, 255, 218, 185)
AURA_COLOR(PERU, 205, 133, 63)
AURA_COLOR(PINK, 255, 192, 203)
AURA_COLOR(PLUM, 221, 160, 221)
AURA_COLOR(POWDER_BLUE, 176, 224, 230)
AURA_COLOR(PURPLE, 128, 0, 128)
AURA_COLOR(REBECCA_PURPLE, 102, 51, 153)
AURA_COLOR(ROSY_BROWN, 188, 143, 143)
AURA_COLOR(ROYAL_BLUE, 65, 105, 225)
AURA_COLOR(SADDLE_BROWN, 139, 69, 19)
AURA_COLOR(SALMON, 250, 128, 114)
AURA_COLOR(SANDY_BROWN, 244, 164, 96)
AURA_COLOR(SEASHELL, 255, 245, 238)
AURA_COLOR(SEA_GREEN, 46, 139, 87)
AURA_COLOR(SIENNA, 160, 82, 45)
AURA_COLOR(SILVER, 192, 192, 192)
AURA_COLOR(SKY_BLUE, 135, 206, 235)
AURA_COLOR(SLATE_BLUE, 106, 90, 205)
AURA_COLOR(SLATE_GRAY, 112, 128, 144)
AURA_COLOR(SNOW, 255, 250, 250)
AURA_COLOR(SPRING_GREEN, 0, 255, 127)
AURA_COLOR(STEEL_BLUE, 70, 130, 180)
AURA_COLOR(TAN, 210, 180, 140)
AURA_COLOR(TEAL, 0, 128, 128)
AURA_COLOR(THISTLE, 216, 191, 216)
AURA_COLOR(TOMATO, 255, 99, 71)
AURA_COLOR(TURQUOISE, 64, 224, 208)
AURA_COLOR(VIOLET, 238, 130, 238)
AURA_COLOR(WHEAT, 245, 222, 179)
AURA_COLOR(WHITE_SMOKE, 245, 245, 245)
AURA_COLOR(YELLOW_GREEN, 154, 205, 50)

// ===============================================
// Material Design Colors
// ===============================================
AURA_COLOR(MAT_RED_500, 244, 67, 54)
AURA_COLOR(MAT_PINK_500, 233, 30, 99)
AURA_COLOR(MAT_PURPLE_500, 156, 39, 176)
AURA_COLOR(MAT_DEEP_PURPLE_500, 103, 58, 183)
AURA_COLOR(MAT_INDIGO_500, 63, 81, 181)
AURA_COLOR(MAT_BLUE_500, 33, 150, 243)
AURA_COLOR(MAT_LIGHT_BLUE_500, 3, 169, 244)
AURA_COLOR(MAT_CYAN_500, 0, 188, 212)
AURA_COLOR(MAT_TEAL_500, 0, 150, 136)
AURA_COLOR(MAT_GREEN_500, 76, 175, 80)
AURA_COLOR(MAT_LIGHT_GREEN_500, 139, 195, 74)
AURA_COLOR(MAT_LIME_500, 205, 220, 57)
AURA_COLOR(MAT_YELLOW_500, 255, 235, 59)
AURA_COLOR(MAT_AMBER_500, 255, 193, 7)
AURA_COLOR(MAT_ORANGE_500, 255, 152, 0)
AURA_COLOR(MAT_DEEP_ORANGE_500, 255, 87, 34)
AURA_COLOR(MAT_BROWN_500, 121, 85, 72)
AURA_COLOR(MAT_GREY_500, 158, 158, 158)
AURA_COLOR(MAT_BLUE_GREY_500, 96, 125, 139)

#undef AURA_COLOR

// ===============================================
// Additional helper functions for Vulkan/OpenGL
// ===============================================

// Helper function to convert RGB to vec3-style array for shader uniforms
inline std::array<f32, 3> toVec3(const RGBf &color)
{
    return {color.r, color.g, color.b};
}

inline float *toVec3(const RGBf &color, f32 *outArray)
{
    outArray[0] = color.r;
    outArray[1] = color.g;
    outArray[2] = color.b;
    return outArray;
}

// Helper function to convert RGB to vec4-style array (with alpha) for shader uniforms
inline std::array<f32, 4> toVec4(const RGBf &color, f32 alpha)
{
    return {color.r, color.g, color.b, alpha};
}

inline float *toVec4(const RGBf &color, f32 alpha, f32 *outArray)
{
    outArray[0] = color.r;
    outArray[1] = color.g;
    outArray[2] = color.b;
    outArray[3] = alpha;
    return outArray;
}

// Convert RGB to float array [r, g, b]
inline void RGBtoFloatArray(const RGB &color, f32 *outArray)
{
    outArray[0] = color.r / 255.0f;
    outArray[1] = color.g / 255.0f;
    outArray[2] = color.b / 255.0f;
}

// Convert RGB to float array [r, g, b, a]
inline void RGBtoFloatArrayWithAlpha(const RGB &color, float alpha, f32 *outArray)
{
    outArray[0] = color.r / 255.0f;
    outArray[1] = color.g / 255.0f;
    outArray[2] = color.b / 255.0f;
    outArray[3] = alpha;
}

} // namespace colors
} // namespace aura3d
#endif // COLOR_DEFINITIONS_H
