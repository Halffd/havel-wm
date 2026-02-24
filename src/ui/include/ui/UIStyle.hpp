#pragma once

#include <ui/UITypes.hpp>
#include <string>
#include <optional>

namespace havel::ui {

/**
 * Font description for text rendering
 */
struct UIFontDesc {
    std::string family = "sans-serif";
    float size = 14.0f;
    bool bold = false;
    bool italic = false;
    bool underline = false;
};

/**
 * Border description
 */
struct UIBorder {
    float width = 0.0f;
    UIColor color = UIColor::transparent();
    
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float left = 0.0f;
    
    static UIBorder uniform(float width, const UIColor& color) {
        return {width, color, width, width, width, width};
    }
    
    static UIBorder none() {
        return {0, UIColor::transparent(), 0, 0, 0, 0};
    }
};

/**
 * Corner radius for rounded rectangles
 */
struct UICornerRadius {
    float topLeft = 0.0f;
    float topRight = 0.0f;
    float bottomRight = 0.0f;
    float bottomLeft = 0.0f;
    
    static UICornerRadius uniform(float radius) {
        return {radius, radius, radius, radius};
    }
    
    static UICornerRadius none() {
        return {0, 0, 0, 0};
    }
};

/**
 * UI Style - complete styling information for elements
 */
struct UIStyle {
    // Background
    UIColor backgroundColor = UIColor::transparent();
    std::optional<UIColor> backgroundColorHover;
    std::optional<UIColor> backgroundColorPressed;
    
    // Border
    UIBorder border = UIBorder::none();
    UICornerRadius cornerRadius = UICornerRadius::none();
    
    // Text
    UIColor textColor = UIColor::white();
    UIFontDesc font;
    
    // Layout
    float minWidth = 0.0f;
    float minHeight = 0.0f;
    float maxWidth = 0.0f;
    float maxHeight = 0.0f;
    
    float marginLeft = 0.0f;
    float marginRight = 0.0f;
    float marginTop = 0.0f;
    float marginBottom = 0.0f;
    
    float paddingLeft = 0.0f;
    float paddingRight = 0.0f;
    float paddingTop = 0.0f;
    float paddingBottom = 0.0f;
    
    // Shadow
    UIColor shadowColor = UIColor::black();
    float shadowBlur = 0.0f;
    float shadowOffsetX = 0.0f;
    float shadowOffsetY = 0.0f;
    
    // Opacity
    float opacity = 1.0f;
    
    // Cursor
    UICursor cursor = UICursor::Arrow;
    
    // Interactivity
    bool pointerEvents = true;
    bool selectable = false;
    
    // Create common styles
    static UIStyle button() {
        UIStyle style;
        style.backgroundColor = UIColor::fromRGB(60, 60, 65);
        style.backgroundColorHover = UIColor::fromRGB(70, 70, 80);
        style.backgroundColorPressed = UIColor::fromRGB(50, 50, 55);
        style.border = UIBorder::uniform(1.0f, UIColor::fromRGB(80, 80, 85));
        style.cornerRadius = UICornerRadius::uniform(4.0f);
        style.textColor = UIColor::white();
        style.font.size = 14.0f;
        style.paddingLeft = 16.0f;
        style.paddingRight = 16.0f;
        style.paddingTop = 8.0f;
        style.paddingBottom = 8.0f;
        style.cursor = UICursor::Hand;
        return style;
    }
    
    static UIStyle panel() {
        UIStyle style;
        style.backgroundColor = UIColor::fromRGBA(30, 30, 35, 240);
        style.border = UIBorder::uniform(1.0f, UIColor::fromRGB(50, 50, 55));
        style.cornerRadius = UICornerRadius::uniform(6.0f);
        style.shadowColor = UIColor::fromRGBA(0, 0, 0, 100);
        style.shadowBlur = 20.0f;
        style.shadowOffsetY = 10.0f;
        return style;
    }
    
    static UIStyle input() {
        UIStyle style;
        style.backgroundColor = UIColor::fromRGB(40, 40, 45);
        style.border = UIBorder::uniform(1.0f, UIColor::fromRGB(70, 70, 75));
        style.cornerRadius = UICornerRadius::uniform(4.0f);
        style.textColor = UIColor::white();
        style.font.size = 14.0f;
        style.paddingLeft = 10.0f;
        style.paddingRight = 10.0f;
        style.paddingTop = 6.0f;
        style.paddingBottom = 6.0f;
        style.selectable = true;
        return style;
    }
    
    static UIStyle label() {
        UIStyle style;
        style.textColor = UIColor::white();
        style.font.size = 14.0f;
        return style;
    }
};

/**
 * UI Theme - collection of styles for consistent look
 */
struct UITheme {
    std::string name = "Dark";
    
    // Colors
    UIColor background = UIColor::fromRGB(25, 25, 30);
    UIColor surface = UIColor::fromRGB(40, 40, 45);
    UIColor surfaceHover = UIColor::fromRGB(50, 50, 55);
    UIColor primary = UIColor::fromRGB(100, 150, 255);
    UIColor secondary = UIColor::fromRGB(150, 150, 160);
    UIColor accent = UIColor::fromRGB(255, 100, 150);
    UIColor text = UIColor::fromRGB(230, 230, 235);
    UIColor textMuted = UIColor::fromRGB(150, 150, 160);
    UIColor border = UIColor::fromRGB(60, 60, 65);
    UIColor error = UIColor::fromRGB(255, 80, 80);
    UIColor success = UIColor::fromRGB(80, 200, 100);
    UIColor warning = UIColor::fromRGB(255, 180, 80);
    
    // Component styles
    UIStyle buttonStyle = UIStyle::button();
    UIStyle panelStyle = UIStyle::panel();
    UIStyle inputStyle = UIStyle::input();
    UIStyle labelStyle = UIStyle::label();
    
    // Font defaults
    UIFontDesc defaultFont = {"sans-serif", 14.0f, false, false, false};
    UIFontDesc titleFont = {"sans-serif", 24.0f, true, false, false};
    UIFontDesc smallFont = {"sans-serif", 12.0f, false, false, false};
};

} // namespace havel::ui
