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

//! CSS 'green' is (0, 128, 0); the GREEN above is the pure-channel primary
//! (0, 255, 0), which CSS calls 'lime'. Both are kept under distinct names.
const RGB WEB_GREEN(0, 128, 0);
const u32 WEB_GREEN_UINT32 = RGB_TO_UINT32(0, 128, 0);
const RGBf WEB_GREEN_F(0.0f, 0.502f, 0.0f);

const RGB GREEN_YELLOW(173, 255, 47);
const u32 GREEN_YELLOW_UINT32 = RGB_TO_UINT32(173, 255, 47);
const RGBf GREEN_YELLOW_F(0.678f, 1.0f, 0.184f);

const RGB HONEYDEW(240, 255, 240);
const u32 HONEYDEW_UINT32 = RGB_TO_UINT32(240, 255, 240);
const RGBf HONEYDEW_F(0.941f, 1.0f, 0.941f);

const RGB HOT_PINK(255, 105, 180);
const u32 HOT_PINK_UINT32 = RGB_TO_UINT32(255, 105, 180);
const RGBf HOT_PINK_F(1.0f, 0.412f, 0.706f);

const RGB INDIAN_RED(205, 92, 92);
const u32 INDIAN_RED_UINT32 = RGB_TO_UINT32(205, 92, 92);
const RGBf INDIAN_RED_F(0.804f, 0.361f, 0.361f);

const RGB INDIGO(75, 0, 130);
const u32 INDIGO_UINT32 = RGB_TO_UINT32(75, 0, 130);
const RGBf INDIGO_F(0.294f, 0.0f, 0.51f);

const RGB IVORY(255, 255, 240);
const u32 IVORY_UINT32 = RGB_TO_UINT32(255, 255, 240);
const RGBf IVORY_F(1.0f, 1.0f, 0.941f);

const RGB KHAKI(240, 230, 140);
const u32 KHAKI_UINT32 = RGB_TO_UINT32(240, 230, 140);
const RGBf KHAKI_F(0.941f, 0.902f, 0.549f);

const RGB LAVENDER(230, 230, 250);
const u32 LAVENDER_UINT32 = RGB_TO_UINT32(230, 230, 250);
const RGBf LAVENDER_F(0.902f, 0.902f, 0.98f);

const RGB LAVENDER_BLUSH(255, 240, 245);
const u32 LAVENDER_BLUSH_UINT32 = RGB_TO_UINT32(255, 240, 245);
const RGBf LAVENDER_BLUSH_F(1.0f, 0.941f, 0.961f);

const RGB LAWN_GREEN(124, 252, 0);
const u32 LAWN_GREEN_UINT32 = RGB_TO_UINT32(124, 252, 0);
const RGBf LAWN_GREEN_F(0.486f, 0.988f, 0.0f);

const RGB LEMON_CHIFFON(255, 250, 205);
const u32 LEMON_CHIFFON_UINT32 = RGB_TO_UINT32(255, 250, 205);
const RGBf LEMON_CHIFFON_F(1.0f, 0.98f, 0.804f);

const RGB LIGHT_BLUE(173, 216, 230);
const u32 LIGHT_BLUE_UINT32 = RGB_TO_UINT32(173, 216, 230);
const RGBf LIGHT_BLUE_F(0.678f, 0.847f, 0.902f);

const RGB LIGHT_CORAL(240, 128, 128);
const u32 LIGHT_CORAL_UINT32 = RGB_TO_UINT32(240, 128, 128);
const RGBf LIGHT_CORAL_F(0.941f, 0.502f, 0.502f);

const RGB LIGHT_CYAN(224, 255, 255);
const u32 LIGHT_CYAN_UINT32 = RGB_TO_UINT32(224, 255, 255);
const RGBf LIGHT_CYAN_F(0.878f, 1.0f, 1.0f);

const RGB LIGHT_GOLDENROD_YELLOW(250, 250, 210);
const u32 LIGHT_GOLDENROD_YELLOW_UINT32 = RGB_TO_UINT32(250, 250, 210);
const RGBf LIGHT_GOLDENROD_YELLOW_F(0.98f, 0.98f, 0.824f);

const RGB LIGHT_GREEN(144, 238, 144);
const u32 LIGHT_GREEN_UINT32 = RGB_TO_UINT32(144, 238, 144);
const RGBf LIGHT_GREEN_F(0.565f, 0.933f, 0.565f);

const RGB LIGHT_PINK(255, 182, 193);
const u32 LIGHT_PINK_UINT32 = RGB_TO_UINT32(255, 182, 193);
const RGBf LIGHT_PINK_F(1.0f, 0.714f, 0.757f);

const RGB LIGHT_SALMON(255, 160, 122);
const u32 LIGHT_SALMON_UINT32 = RGB_TO_UINT32(255, 160, 122);
const RGBf LIGHT_SALMON_F(1.0f, 0.627f, 0.478f);

const RGB LIGHT_SEA_GREEN(32, 178, 170);
const u32 LIGHT_SEA_GREEN_UINT32 = RGB_TO_UINT32(32, 178, 170);
const RGBf LIGHT_SEA_GREEN_F(0.125f, 0.698f, 0.667f);

const RGB LIGHT_SKY_BLUE(135, 206, 250);
const u32 LIGHT_SKY_BLUE_UINT32 = RGB_TO_UINT32(135, 206, 250);
const RGBf LIGHT_SKY_BLUE_F(0.529f, 0.808f, 0.98f);

const RGB LIGHT_SLATE_GRAY(119, 136, 153);
const u32 LIGHT_SLATE_GRAY_UINT32 = RGB_TO_UINT32(119, 136, 153);
const RGBf LIGHT_SLATE_GRAY_F(0.467f, 0.533f, 0.6f);

const RGB LIGHT_STEEL_BLUE(176, 196, 222);
const u32 LIGHT_STEEL_BLUE_UINT32 = RGB_TO_UINT32(176, 196, 222);
const RGBf LIGHT_STEEL_BLUE_F(0.69f, 0.769f, 0.871f);

const RGB LIGHT_YELLOW(255, 255, 224);
const u32 LIGHT_YELLOW_UINT32 = RGB_TO_UINT32(255, 255, 224);
const RGBf LIGHT_YELLOW_F(1.0f, 1.0f, 0.878f);

const RGB LIME_GREEN(50, 205, 50);
const u32 LIME_GREEN_UINT32 = RGB_TO_UINT32(50, 205, 50);
const RGBf LIME_GREEN_F(0.196f, 0.804f, 0.196f);

const RGB LINEN(250, 240, 230);
const u32 LINEN_UINT32 = RGB_TO_UINT32(250, 240, 230);
const RGBf LINEN_F(0.98f, 0.941f, 0.902f);

const RGB MAROON(128, 0, 0);
const u32 MAROON_UINT32 = RGB_TO_UINT32(128, 0, 0);
const RGBf MAROON_F(0.502f, 0.0f, 0.0f);

const RGB MEDIUM_AQUAMARINE(102, 205, 170);
const u32 MEDIUM_AQUAMARINE_UINT32 = RGB_TO_UINT32(102, 205, 170);
const RGBf MEDIUM_AQUAMARINE_F(0.4f, 0.804f, 0.667f);

const RGB MEDIUM_BLUE(0, 0, 205);
const u32 MEDIUM_BLUE_UINT32 = RGB_TO_UINT32(0, 0, 205);
const RGBf MEDIUM_BLUE_F(0.0f, 0.0f, 0.804f);

const RGB MEDIUM_ORCHID(186, 85, 211);
const u32 MEDIUM_ORCHID_UINT32 = RGB_TO_UINT32(186, 85, 211);
const RGBf MEDIUM_ORCHID_F(0.729f, 0.333f, 0.827f);

const RGB MEDIUM_PURPLE(147, 112, 219);
const u32 MEDIUM_PURPLE_UINT32 = RGB_TO_UINT32(147, 112, 219);
const RGBf MEDIUM_PURPLE_F(0.576f, 0.439f, 0.859f);

const RGB MEDIUM_SEA_GREEN(60, 179, 113);
const u32 MEDIUM_SEA_GREEN_UINT32 = RGB_TO_UINT32(60, 179, 113);
const RGBf MEDIUM_SEA_GREEN_F(0.235f, 0.702f, 0.443f);

const RGB MEDIUM_SLATE_BLUE(123, 104, 238);
const u32 MEDIUM_SLATE_BLUE_UINT32 = RGB_TO_UINT32(123, 104, 238);
const RGBf MEDIUM_SLATE_BLUE_F(0.482f, 0.408f, 0.933f);

const RGB MEDIUM_SPRING_GREEN(0, 250, 154);
const u32 MEDIUM_SPRING_GREEN_UINT32 = RGB_TO_UINT32(0, 250, 154);
const RGBf MEDIUM_SPRING_GREEN_F(0.0f, 0.98f, 0.604f);

const RGB MEDIUM_TURQUOISE(72, 209, 204);
const u32 MEDIUM_TURQUOISE_UINT32 = RGB_TO_UINT32(72, 209, 204);
const RGBf MEDIUM_TURQUOISE_F(0.282f, 0.82f, 0.8f);

const RGB MEDIUM_VIOLET_RED(199, 21, 133);
const u32 MEDIUM_VIOLET_RED_UINT32 = RGB_TO_UINT32(199, 21, 133);
const RGBf MEDIUM_VIOLET_RED_F(0.78f, 0.082f, 0.522f);

const RGB MIDNIGHT_BLUE(25, 25, 112);
const u32 MIDNIGHT_BLUE_UINT32 = RGB_TO_UINT32(25, 25, 112);
const RGBf MIDNIGHT_BLUE_F(0.098f, 0.098f, 0.439f);

const RGB MINT_CREAM(245, 255, 250);
const u32 MINT_CREAM_UINT32 = RGB_TO_UINT32(245, 255, 250);
const RGBf MINT_CREAM_F(0.961f, 1.0f, 0.98f);

const RGB MISTY_ROSE(255, 228, 225);
const u32 MISTY_ROSE_UINT32 = RGB_TO_UINT32(255, 228, 225);
const RGBf MISTY_ROSE_F(1.0f, 0.894f, 0.882f);

const RGB MOCCASIN(255, 228, 181);
const u32 MOCCASIN_UINT32 = RGB_TO_UINT32(255, 228, 181);
const RGBf MOCCASIN_F(1.0f, 0.894f, 0.71f);

const RGB NAVAJO_WHITE(255, 222, 173);
const u32 NAVAJO_WHITE_UINT32 = RGB_TO_UINT32(255, 222, 173);
const RGBf NAVAJO_WHITE_F(1.0f, 0.871f, 0.678f);

const RGB NAVY(0, 0, 128);
const u32 NAVY_UINT32 = RGB_TO_UINT32(0, 0, 128);
const RGBf NAVY_F(0.0f, 0.0f, 0.502f);

const RGB OLD_LACE(253, 245, 230);
const u32 OLD_LACE_UINT32 = RGB_TO_UINT32(253, 245, 230);
const RGBf OLD_LACE_F(0.992f, 0.961f, 0.902f);

const RGB OLIVE(128, 128, 0);
const u32 OLIVE_UINT32 = RGB_TO_UINT32(128, 128, 0);
const RGBf OLIVE_F(0.502f, 0.502f, 0.0f);

const RGB OLIVE_DRAB(107, 142, 35);
const u32 OLIVE_DRAB_UINT32 = RGB_TO_UINT32(107, 142, 35);
const RGBf OLIVE_DRAB_F(0.42f, 0.557f, 0.137f);

const RGB ORANGE(255, 165, 0);
const u32 ORANGE_UINT32 = RGB_TO_UINT32(255, 165, 0);
const RGBf ORANGE_F(1.0f, 0.647f, 0.0f);

const RGB ORANGE_RED(255, 69, 0);
const u32 ORANGE_RED_UINT32 = RGB_TO_UINT32(255, 69, 0);
const RGBf ORANGE_RED_F(1.0f, 0.271f, 0.0f);

const RGB ORCHID(218, 112, 214);
const u32 ORCHID_UINT32 = RGB_TO_UINT32(218, 112, 214);
const RGBf ORCHID_F(0.855f, 0.439f, 0.839f);

const RGB PALE_GOLDENROD(238, 232, 170);
const u32 PALE_GOLDENROD_UINT32 = RGB_TO_UINT32(238, 232, 170);
const RGBf PALE_GOLDENROD_F(0.933f, 0.91f, 0.667f);

const RGB PALE_GREEN(152, 251, 152);
const u32 PALE_GREEN_UINT32 = RGB_TO_UINT32(152, 251, 152);
const RGBf PALE_GREEN_F(0.596f, 0.984f, 0.596f);

const RGB PALE_TURQUOISE(175, 238, 238);
const u32 PALE_TURQUOISE_UINT32 = RGB_TO_UINT32(175, 238, 238);
const RGBf PALE_TURQUOISE_F(0.686f, 0.933f, 0.933f);

const RGB PALE_VIOLET_RED(219, 112, 147);
const u32 PALE_VIOLET_RED_UINT32 = RGB_TO_UINT32(219, 112, 147);
const RGBf PALE_VIOLET_RED_F(0.859f, 0.439f, 0.576f);

const RGB PAPAYA_WHIP(255, 239, 213);
const u32 PAPAYA_WHIP_UINT32 = RGB_TO_UINT32(255, 239, 213);
const RGBf PAPAYA_WHIP_F(1.0f, 0.937f, 0.835f);

const RGB PEACH_PUFF(255, 218, 185);
const u32 PEACH_PUFF_UINT32 = RGB_TO_UINT32(255, 218, 185);
const RGBf PEACH_PUFF_F(1.0f, 0.855f, 0.725f);

const RGB PERU(205, 133, 63);
const u32 PERU_UINT32 = RGB_TO_UINT32(205, 133, 63);
const RGBf PERU_F(0.804f, 0.522f, 0.247f);

const RGB PINK(255, 192, 203);
const u32 PINK_UINT32 = RGB_TO_UINT32(255, 192, 203);
const RGBf PINK_F(1.0f, 0.753f, 0.796f);

const RGB PLUM(221, 160, 221);
const u32 PLUM_UINT32 = RGB_TO_UINT32(221, 160, 221);
const RGBf PLUM_F(0.867f, 0.627f, 0.867f);

const RGB POWDER_BLUE(176, 224, 230);
const u32 POWDER_BLUE_UINT32 = RGB_TO_UINT32(176, 224, 230);
const RGBf POWDER_BLUE_F(0.69f, 0.878f, 0.902f);

const RGB PURPLE(128, 0, 128);
const u32 PURPLE_UINT32 = RGB_TO_UINT32(128, 0, 128);
const RGBf PURPLE_F(0.502f, 0.0f, 0.502f);

const RGB REBECCA_PURPLE(102, 51, 153);
const u32 REBECCA_PURPLE_UINT32 = RGB_TO_UINT32(102, 51, 153);
const RGBf REBECCA_PURPLE_F(0.4f, 0.2f, 0.6f);

const RGB ROSY_BROWN(188, 143, 143);
const u32 ROSY_BROWN_UINT32 = RGB_TO_UINT32(188, 143, 143);
const RGBf ROSY_BROWN_F(0.737f, 0.561f, 0.561f);

const RGB ROYAL_BLUE(65, 105, 225);
const u32 ROYAL_BLUE_UINT32 = RGB_TO_UINT32(65, 105, 225);
const RGBf ROYAL_BLUE_F(0.255f, 0.412f, 0.882f);

const RGB SADDLE_BROWN(139, 69, 19);
const u32 SADDLE_BROWN_UINT32 = RGB_TO_UINT32(139, 69, 19);
const RGBf SADDLE_BROWN_F(0.545f, 0.271f, 0.075f);

const RGB SALMON(250, 128, 114);
const u32 SALMON_UINT32 = RGB_TO_UINT32(250, 128, 114);
const RGBf SALMON_F(0.98f, 0.502f, 0.447f);

const RGB SANDY_BROWN(244, 164, 96);
const u32 SANDY_BROWN_UINT32 = RGB_TO_UINT32(244, 164, 96);
const RGBf SANDY_BROWN_F(0.957f, 0.643f, 0.376f);

const RGB SEASHELL(255, 245, 238);
const u32 SEASHELL_UINT32 = RGB_TO_UINT32(255, 245, 238);
const RGBf SEASHELL_F(1.0f, 0.961f, 0.933f);

const RGB SEA_GREEN(46, 139, 87);
const u32 SEA_GREEN_UINT32 = RGB_TO_UINT32(46, 139, 87);
const RGBf SEA_GREEN_F(0.18f, 0.545f, 0.341f);

const RGB SIENNA(160, 82, 45);
const u32 SIENNA_UINT32 = RGB_TO_UINT32(160, 82, 45);
const RGBf SIENNA_F(0.627f, 0.322f, 0.176f);

const RGB SILVER(192, 192, 192);
const u32 SILVER_UINT32 = RGB_TO_UINT32(192, 192, 192);
const RGBf SILVER_F(0.753f, 0.753f, 0.753f);

const RGB SKY_BLUE(135, 206, 235);
const u32 SKY_BLUE_UINT32 = RGB_TO_UINT32(135, 206, 235);
const RGBf SKY_BLUE_F(0.529f, 0.808f, 0.922f);

const RGB SLATE_BLUE(106, 90, 205);
const u32 SLATE_BLUE_UINT32 = RGB_TO_UINT32(106, 90, 205);
const RGBf SLATE_BLUE_F(0.416f, 0.353f, 0.804f);

const RGB SLATE_GRAY(112, 128, 144);
const u32 SLATE_GRAY_UINT32 = RGB_TO_UINT32(112, 128, 144);
const RGBf SLATE_GRAY_F(0.439f, 0.502f, 0.565f);

const RGB SNOW(255, 250, 250);
const u32 SNOW_UINT32 = RGB_TO_UINT32(255, 250, 250);
const RGBf SNOW_F(1.0f, 0.98f, 0.98f);

const RGB SPRING_GREEN(0, 255, 127);
const u32 SPRING_GREEN_UINT32 = RGB_TO_UINT32(0, 255, 127);
const RGBf SPRING_GREEN_F(0.0f, 1.0f, 0.498f);

const RGB STEEL_BLUE(70, 130, 180);
const u32 STEEL_BLUE_UINT32 = RGB_TO_UINT32(70, 130, 180);
const RGBf STEEL_BLUE_F(0.275f, 0.51f, 0.706f);

const RGB TAN(210, 180, 140);
const u32 TAN_UINT32 = RGB_TO_UINT32(210, 180, 140);
const RGBf TAN_F(0.824f, 0.706f, 0.549f);

const RGB TEAL(0, 128, 128);
const u32 TEAL_UINT32 = RGB_TO_UINT32(0, 128, 128);
const RGBf TEAL_F(0.0f, 0.502f, 0.502f);

const RGB THISTLE(216, 191, 216);
const u32 THISTLE_UINT32 = RGB_TO_UINT32(216, 191, 216);
const RGBf THISTLE_F(0.847f, 0.749f, 0.847f);

const RGB TOMATO(255, 99, 71);
const u32 TOMATO_UINT32 = RGB_TO_UINT32(255, 99, 71);
const RGBf TOMATO_F(1.0f, 0.388f, 0.278f);

const RGB TURQUOISE(64, 224, 208);
const u32 TURQUOISE_UINT32 = RGB_TO_UINT32(64, 224, 208);
const RGBf TURQUOISE_F(0.251f, 0.878f, 0.816f);

const RGB VIOLET(238, 130, 238);
const u32 VIOLET_UINT32 = RGB_TO_UINT32(238, 130, 238);
const RGBf VIOLET_F(0.933f, 0.51f, 0.933f);

const RGB WHEAT(245, 222, 179);
const u32 WHEAT_UINT32 = RGB_TO_UINT32(245, 222, 179);
const RGBf WHEAT_F(0.961f, 0.871f, 0.702f);

const RGB WHITE_SMOKE(245, 245, 245);
const u32 WHITE_SMOKE_UINT32 = RGB_TO_UINT32(245, 245, 245);
const RGBf WHITE_SMOKE_F(0.961f, 0.961f, 0.961f);

const RGB YELLOW_GREEN(154, 205, 50);
const u32 YELLOW_GREEN_UINT32 = RGB_TO_UINT32(154, 205, 50);
const RGBf YELLOW_GREEN_F(0.604f, 0.804f, 0.196f);


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
