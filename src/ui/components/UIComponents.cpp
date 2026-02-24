#include <ui/UIComponents.hpp>
#include <ui/UIContext.hpp>
#include <cmath>
#include <algorithm>

namespace havel::ui {

// ============================================================================
// UIButton Implementation
// ============================================================================

UIButton::UIButton() {
    m_style = UIStyle::button();
}

bool UIButton::handleEvent(const UIEvent& event) {
    if (!m_enabled) return false;
    
    switch (event.type) {
        case UIEventType::MouseDown:
            if (event.mouseButton == UIMouseButton::Left) {
                m_pressed = true;
                return true;
            }
            break;
            
        case UIEventType::MouseUp:
            if (m_pressed && event.mouseButton == UIMouseButton::Left) {
                m_pressed = false;
                
                if (m_toggleable) {
                    m_toggled = !m_toggled;
                }
                
                if (m_clickCallback) {
                    m_clickCallback(this);
                }
                
                return true;
            }
            break;
            
        case UIEventType::MouseLeave:
            m_pressed = false;
            break;
    }
    
    return UIElement::handleEvent(event);
}

void UIButton::render(UIContext& context) {
    // Determine background color based on state
    UIColor bgColor = m_style.backgroundColor;
    
    if (!m_enabled) {
        bgColor = UIColor{bgColor.r * 0.5f, bgColor.g * 0.5f, bgColor.b * 0.5f, bgColor.a};
    } else if (m_pressed) {
        if (m_style.backgroundColorPressed) {
            bgColor = *m_style.backgroundColorPressed;
        }
    } else if (m_hovered) {
        if (m_style.backgroundColorHover) {
            bgColor = *m_style.backgroundColorHover;
        }
    }
    
    // Render button background
    context.renderRect(m_rect, bgColor, m_style.cornerRadius.topLeft);
    
    // Render border
    if (m_style.border.width > 0) {
        context.renderBorder(m_rect, m_style.border, m_style.cornerRadius);
    }
    
    // Render text
    float textX = m_rect.x + m_style.paddingLeft;
    float textY = m_rect.y + m_style.paddingTop;
    context.renderText(m_text, textX, textY, m_style.textColor, m_style.font.size);
}

void UIButton::update(float deltaTime) {
    (void)deltaTime;
}

// ============================================================================
// UILabel Implementation
// ============================================================================

UILabel::UILabel() {
    m_style = UIStyle::label();
}

void UILabel::render(UIContext& context) {
    float textX = m_rect.x;
    float textY = m_rect.y;
    
    // Apply alignment
    switch (m_alignment) {
        case Alignment::Center:
            textX = m_rect.x + (m_rect.w / 2.0f);
            break;
        case Alignment::Right:
            textX = m_rect.x + m_rect.w;
            break;
        default:
            break;
    }
    
    context.renderText(m_text, textX, textY, m_style.textColor, m_style.font.size);
}

// ============================================================================
// UIPanel Implementation
// ============================================================================

UIPanel::UIPanel() {
    m_style = UIStyle::panel();
}

bool UIPanel::handleEvent(const UIEvent& event) {
    if (!m_enabled) return false;
    
    if (m_draggable && m_title.empty()) {
        // Entire panel is draggable
        switch (event.type) {
            case UIEventType::MouseDown:
                if (event.mouseButton == UIMouseButton::Left) {
                    m_isDragging = true;
                    m_dragOffsetX = event.mouseX - m_rect.x;
                    m_dragOffsetY = event.mouseY - m_rect.y;
                    return true;
                }
                break;
                
            case UIEventType::MouseMove:
                if (m_isDragging) {
                    setPosition(event.mouseX - m_dragOffsetX, event.mouseY - m_dragOffsetY);
                    return true;
                }
                break;
                
            case UIEventType::MouseUp:
                m_isDragging = false;
                break;
        }
    }
    
    return UIElement::handleEvent(event);
}

void UIPanel::render(UIContext& context) {
    if (m_collapsed) return;
    
    // Render panel background
    context.renderRect(m_rect, m_style.backgroundColor, m_style.cornerRadius.topLeft);
    
    // Render border
    if (m_style.border.width > 0) {
        context.renderBorder(m_rect, m_style.border, m_style.cornerRadius);
    }
    
    // Render title bar if has title
    if (!m_title.empty()) {
        UIRect titleRect = {
            m_rect.x,
            m_rect.y,
            m_rect.w,
            30.0f  // Title bar height
        };
        
        UIColor titleBg = m_style.backgroundColor;
        titleBg.r *= 0.8f;
        titleBg.g *= 0.8f;
        titleBg.b *= 0.8f;
        
        context.renderRect(titleRect, titleBg, m_style.cornerRadius.topLeft);
        context.renderText(m_title, titleRect.x + 10, titleRect.y + 8, m_style.textColor, 14.0f);
        
        // Render close button if closable
        if (m_closable) {
            UIRect closeBtn = {
                titleRect.x + titleRect.w - 25,
                titleRect.y + 5,
                20,
                20
            };
            context.renderRect(closeBtn, UIColor::fromRGB(200, 50, 50), 3.0f);
            context.renderText("×", closeBtn.x + 6, closeBtn.y + 4, UIColor::white(), 16.0f);
        }
    }
    
    // Render children
    for (const auto& child : m_children) {
        if (child->isRenderable()) {
            child->render(context);
        }
    }
}

// ============================================================================
// UIInput Implementation
// ============================================================================

UIInput::UIInput() {
    m_style = UIStyle::input();
    m_style.selectable = true;
}

std::string UIInput::getSelectedText() const {
    if (m_selectionStart < 0 || m_selectionEnd < 0) return "";
    
    int start = std::min(m_selectionStart, m_selectionEnd);
    int end = std::max(m_selectionStart, m_selectionEnd);
    
    if (start >= static_cast<int>(m_text.length())) return "";
    if (end > static_cast<int>(m_text.length())) end = m_text.length();
    
    return m_text.substr(start, end - start);
}

bool UIInput::handleEvent(const UIEvent& event) {
    if (!m_enabled || m_readOnly) return false;
    
    switch (event.type) {
        case UIEventType::MouseDown:
            setFocused(true);
            // Calculate cursor position from click
            // (simplified - would need text measurement)
            m_cursorPos = static_cast<int>(m_text.length());
            return true;
            
        case UIEventType::TextInput:
            if (m_focused && event.textChar != 0) {
                // Insert character at cursor
                if (m_maxLength == 0 || static_cast<int>(m_text.length()) < m_maxLength) {
                    m_text.insert(m_cursorPos, 1, event.textChar);
                    m_cursorPos++;
                    
                    if (m_changeCallback) {
                        m_changeCallback(this, m_text);
                    }
                }
                return true;
            }
            break;
            
        case UIEventType::KeyDown:
            if (!m_focused) return false;
            
            switch (event.keyCode) {
                case 119:  // Backspace
                    if (m_cursorPos > 0) {
                        m_text.erase(m_cursorPos - 1, 1);
                        m_cursorPos--;
                        if (m_changeCallback) m_changeCallback(this, m_text);
                    }
                    return true;
                    
                case 113:  // Delete
                    if (m_cursorPos < static_cast<int>(m_text.length())) {
                        m_text.erase(m_cursorPos, 1);
                        if (m_changeCallback) m_changeCallback(this, m_text);
                    }
                    return true;
                    
                case 115:  // Left arrow
                    if (m_cursorPos > 0) m_cursorPos--;
                    return true;
                    
                case 116:  // Right arrow
                    if (m_cursorPos < static_cast<int>(m_text.length())) m_cursorPos++;
                    return true;
                    
                case 28:  // Enter/Return
                    if (m_submitCallback) m_submitCallback(this);
                    return true;
            }
            break;
    }
    
    return UIElement::handleEvent(event);
}

void UIInput::render(UIContext& context) {
    // Render background
    UIColor bgColor = m_style.backgroundColor;
    if (m_focused) {
        bgColor.r += 0.05f;
        bgColor.g += 0.05f;
        bgColor.b += 0.05f;
    }
    
    context.renderRect(m_rect, bgColor, m_style.cornerRadius.topLeft);

    // Render border (highlight when focused)
    UIBorder border = m_style.border;
    if (m_focused) {
        border.color = UIColor::fromRGB(100, 150, 255);
        border.width = 2.0f;
    }
    context.renderBorder(m_rect, border, m_style.cornerRadius);
    
    // Render text or placeholder
    const std::string& displayText = m_text.empty() ? m_placeholder : m_text;
    UIColor textColor = m_text.empty() ? UIColor::fromRGB(120, 120, 120) : m_style.textColor;
    
    float textX = m_rect.x + m_style.paddingLeft;
    float textY = m_rect.y + m_style.paddingTop;
    
    // Apply scroll offset for long text
    textX -= m_scrollOffset;
    
    context.renderText(displayText, textX, textY, textColor, m_style.font.size);
    
    // Render cursor
    if (m_focused && m_showCursor) {
        float cursorX = textX;  // Simplified - would need text measurement
        UIRect cursorRect = {cursorX, m_rect.y + m_style.paddingTop, 1.0f, m_style.font.size};
        context.renderRect(cursorRect, m_style.textColor);
    }
}

void UIInput::update(float deltaTime) {
    // Blink cursor
    m_cursorBlinkTimer += deltaTime;
    if (m_cursorBlinkTimer > 0.5f) {
        m_showCursor = !m_showCursor;
        m_cursorBlinkTimer = 0.0f;
    }
}

// ============================================================================
// UICheckbox Implementation
// ============================================================================

UICheckbox::UICheckbox() {
    setSize(20, 20);
}

UICheckbox::State UICheckbox::getState() const {
    if (m_triState && !m_checked) {
        return State::Indeterminate;
    }
    return m_checked ? State::Checked : State::Unchecked;
}

bool UICheckbox::handleEvent(const UIEvent& event) {
    if (!m_enabled) return false;
    
    if (event.type == UIEventType::MouseClick && event.mouseButton == UIMouseButton::Left) {
        if (m_triState) {
            // Cycle through states
            if (!m_checked) {
                m_checked = true;
            } else {
                m_checked = false;
            }
        } else {
            m_checked = !m_checked;
        }
        
        if (m_changeCallback) {
            m_changeCallback(this, m_checked);
        }
        
        return true;
    }
    
    return UIElement::handleEvent(event);
}

void UICheckbox::render(UIContext& context) {
    // Render checkbox background
    UIColor bgColor = m_hovered ? UIColor::fromRGB(50, 50, 55) : UIColor::fromRGB(40, 40, 45);
    context.renderRect(m_rect, bgColor, 3.0f);
    
    // Render border
    UIColor borderColor = m_focused ? UIColor::fromRGB(100, 150, 255) : UIColor::fromRGB(70, 70, 75);
    UIBorder border = UIBorder::uniform(1.0f, borderColor);
    context.renderBorder(m_rect, border, UICornerRadius::uniform(3.0f));
    
    // Render checkmark or indeterminate
    if (m_checked) {
        context.renderText("✓", m_rect.x + 4, m_rect.y + 2, UIColor::white(), 16.0f);
    } else if (m_triState && getState() == State::Indeterminate) {
        context.renderText("−", m_rect.x + 5, m_rect.y + 2, UIColor::white(), 16.0f);
    }
    
    // Render label if present
    if (!m_label.empty()) {
        context.renderText(m_label, m_rect.x + m_rect.w + 8, m_rect.y, m_style.textColor, m_style.font.size);
    }
}

// ============================================================================
// UISlider Implementation
// ============================================================================

UISlider::UISlider() {
    if (m_orientation == Orientation::Horizontal) {
        setSize(100, 20);
    } else {
        setSize(20, 100);
    }
}

void UISlider::setValue(float value) {
    m_value = std::clamp(value, m_min, m_max);
    
    // Snap to step
    if (m_step > 0) {
        m_value = std::round(m_value / m_step) * m_step;
    }
}

bool UISlider::handleEvent(const UIEvent& event) {
    if (!m_enabled) return false;
    
    switch (event.type) {
        case UIEventType::MouseDown:
            if (event.mouseButton == UIMouseButton::Left) {
                m_isDragging = true;
                // Calculate value from position
                float pos = (m_orientation == Orientation::Horizontal) 
                    ? event.mouseX - m_rect.x 
                    : event.mouseY - m_rect.y;
                float len = (m_orientation == Orientation::Horizontal) 
                    ? m_rect.w : m_rect.h;
                setValue(m_min + (pos / len) * (m_max - m_min));
                return true;
            }
            break;
            
        case UIEventType::MouseMove:
            if (m_isDragging) {
                float pos = (m_orientation == Orientation::Horizontal) 
                    ? event.mouseX - m_rect.x 
                    : event.mouseY - m_rect.y;
                float len = (m_orientation == Orientation::Horizontal) 
                    ? m_rect.w : m_rect.h;
                setValue(m_min + (pos / len) * (m_max - m_min));
                
                if (m_changeCallback) {
                    m_changeCallback(this, m_value);
                }
                return true;
            }
            break;
            
        case UIEventType::MouseUp:
            m_isDragging = false;
            break;
    }
    
    return UIElement::handleEvent(event);
}

void UISlider::render(UIContext& context) {
    // Render track
    UIColor trackColor = UIColor::fromRGB(50, 50, 55);
    UIRect trackRect;
    
    if (m_orientation == Orientation::Horizontal) {
        trackRect = {m_rect.x, m_rect.y + m_rect.h / 2 - 3, m_rect.w, 6};
    } else {
        trackRect = {m_rect.x + m_rect.w / 2 - 3, m_rect.y, 6, m_rect.h};
    }
    
    context.renderRect(trackRect, trackColor, 3.0f);
    
    // Render fill (portion before handle)
    float fillPercent = (m_value - m_min) / (m_max - m_min);
    UIColor fillColor = UIColor::fromRGB(100, 150, 255);
    
    UIRect fillRect;
    if (m_orientation == Orientation::Horizontal) {
        fillRect = {m_rect.x, m_rect.y + m_rect.h / 2 - 3, m_rect.w * fillPercent, 6};
    } else {
        float fillH = m_rect.h * fillPercent;
        fillRect = {m_rect.x + m_rect.w / 2 - 3, m_rect.y + m_rect.h - fillH, 6, fillH};
    }
    
    context.renderRect(fillRect, fillColor, 3.0f);
    
    // Render handle
    float handlePos = (m_orientation == Orientation::Horizontal)
        ? m_rect.x + (m_rect.w - 20) * fillPercent
        : m_rect.y + (m_rect.h - 20) * (1.0f - fillPercent);
    
    UIRect handleRect;
    if (m_orientation == Orientation::Horizontal) {
        handleRect = {handlePos, m_rect.y + m_rect.h / 2 - 10, 20, 20};
    } else {
        handleRect = {m_rect.x + m_rect.w / 2 - 10, handlePos, 20, 20};
    }
    
    UIColor handleColor = m_hovered || m_isDragging 
        ? UIColor::fromRGB(120, 170, 255) 
        : UIColor::fromRGB(100, 150, 255);
    
    context.renderRect(handleRect, handleColor, 10.0f);
}

} // namespace havel::ui
