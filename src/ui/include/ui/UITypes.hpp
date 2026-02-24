#pragma once

#include <cstdint>
#include <string>

namespace havel::ui {

/**
 * 2D Rectangle for UI layout
 */
struct UIRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }

    bool intersects(const UIRect& other) const {
        return x < other.x + other.w && x + w > other.x &&
               y < other.y + other.h && y + h > other.y;
    }
};

/**
 * RGBA Color
 */
struct UIColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    bool operator==(const UIColor& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }

    bool operator!=(const UIColor& other) const {
        return !(*this == other);
    }

    static UIColor fromHex(uint32_t hex) {
        return {
            ((hex >> 24) & 0xFF) / 255.0f,
            ((hex >> 16) & 0xFF) / 255.0f,
            ((hex >> 8) & 0xFF) / 255.0f,
            (hex & 0xFF) / 255.0f
        };
    }

    static UIColor fromRGB(uint8_t r, uint8_t g, uint8_t b) {
        return {r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
    }

    static UIColor fromRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    }

    // Common colors
    static UIColor transparent() { return {0, 0, 0, 0}; }
    static UIColor white() { return {1, 1, 1, 1}; }
    static UIColor black() { return {0, 0, 0, 1}; }
    static UIColor red() { return {1, 0, 0, 1}; }
    static UIColor green() { return {0, 1, 0, 1}; }
    static UIColor blue() { return {0, 0, 1, 1}; }
};

/**
 * UI Alignment options
 */
enum class UIAlign {
    TopLeft,
    TopCenter,
    TopRight,
    MiddleLeft,
    MiddleCenter,
    MiddleRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
    Stretch
};

/**
 * UI Visibility options
 */
enum class UIVisibility {
    Visible,
    Hidden,
    Collapsed  // Hidden and doesn't take layout space
};

/**
 * UI Cursor types
 */
enum class UICursor {
    Arrow,
    IBeam,
    Hand,
    ResizeHorizontal,
    ResizeVertical,
    ResizeDiagonal1,
    ResizeDiagonal2,
    Move,
    NotAllowed
};

} // namespace havel::ui
