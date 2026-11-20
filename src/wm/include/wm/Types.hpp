#pragma once

#include <cstdint>

namespace havel {

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool isValid() const { return w > 0 && h > 0; }
};

/**
 * Color for rendering (RGBA floats)
 */
struct Color {
    float r, g, b, a;
    
    constexpr Color(float red = 1.0f, float green = 1.0f, 
                    float blue = 1.0f, float alpha = 1.0f)
        : r(red), g(green), b(blue), a(alpha) {}
    
    static constexpr Color White() { return Color(1.0f, 1.0f, 1.0f, 1.0f); }
    static constexpr Color Black() { return Color(0.0f, 0.0f, 0.0f, 1.0f); }
    static constexpr Color Red() { return Color(1.0f, 0.0f, 0.0f, 1.0f); }
    static constexpr Color Green() { return Color(0.0f, 1.0f, 0.0f, 1.0f); }
    static constexpr Color Blue() { return Color(0.0f, 0.0f, 1.0f, 1.0f); }
};

constexpr uint32_t WORKSPACE_COUNT = 10;
constexpr uint32_t INVALID_WORKSPACE = UINT32_MAX;

} // namespace havel
