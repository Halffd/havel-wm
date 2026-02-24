#pragma once

#include <ui/UIElement.hpp>
#include <string>

namespace havel::ui {

/**
 * Button component
 */
class UIButton : public UIElement {
public:
    UIButton();
    
    // Text
    void setText(const std::string& text) { m_text = text; }
    const std::string& getText() const { return m_text; }
    
    // Icon (optional)
    void setIcon(const std::string& icon) { m_icon = icon; }
    const std::string& getIcon() const { return m_icon; }
    
    // State
    void setPressed(bool pressed) { m_pressed = pressed; }
    bool isPressed() const { return m_pressed; }
    
    void setToggleable(bool toggleable) { m_toggleable = toggleable; }
    bool isToggleable() const { return m_toggleable; }
    
    void setToggled(bool toggled) { m_toggled = toggled; }
    bool isToggled() const { return m_toggled; }

    // Events
    using ClickCallback = std::function<void(UIButton*)>;
    void setOnClick(ClickCallback cb) { m_clickCallback = cb; }

    // Override
    bool handleEvent(const UIEvent& event) override;
    void render(UIContext& context) override;
    void update(float deltaTime) override;

private:
    std::string m_text;
    std::string m_icon;
    
    bool m_pressed = false;
    bool m_toggleable = false;
    bool m_toggled = false;
    
    ClickCallback m_clickCallback;
};

/**
 * Label component for displaying text
 */
class UILabel : public UIElement {
public:
    UILabel();
    
    // Text
    void setText(const std::string& text) { m_text = text; }
    const std::string& getText() const { return m_text; }
    
    // Text alignment
    enum class Alignment {
        Left,
        Center,
        Right
    };
    
    void setAlignment(Alignment align) { m_alignment = align; }
    Alignment getAlignment() const { return m_alignment; }
    
    // Text wrapping
    void setWrap(bool wrap) { m_wrap = wrap; }
    bool getWrap() const { return m_wrap; }
    
    // Ellipsis for overflow
    void setEllipsis(bool ellipsis) { m_ellipsis = ellipsis; }
    bool getEllipsis() const { return m_ellipsis; }

    // Override
    void render(UIContext& context) override;

private:
    std::string m_text;
    Alignment m_alignment = Alignment::Left;
    bool m_wrap = false;
    bool m_ellipsis = false;
};

/**
 * Panel component - container with background and border
 */
class UIPanel : public UIElement {
public:
    UIPanel();
    
    // Title (optional)
    void setTitle(const std::string& title) { m_title = title; }
    const std::string& getTitle() const { return m_title; }
    
    // Resizable
    void setResizable(bool resizable) { m_resizable = resizable; }
    bool isResizable() const { return m_resizable; }
    
    // Draggable
    void setDraggable(bool draggable) { m_draggable = draggable; }
    bool isDraggable() const { return m_draggable; }
    
    // Closable
    void setClosable(bool closable) { m_closable = closable; }
    bool isClosable() const { return m_closable; }
    
    // Collapsible
    void setCollapsible(bool collapsible) { m_collapsible = collapsible; }
    bool isCollapsible() const { return m_collapsible; }
    void setCollapsed(bool collapsed) { m_collapsed = collapsed; }
    bool isCollapsed() const { return m_collapsed; }

    // Events
    using CloseCallback = std::function<void(UIPanel*)>;
    void setOnClose(CloseCallback cb) { m_closeCallback = cb; }

    // Override
    bool handleEvent(const UIEvent& event) override;
    void render(UIContext& context) override;

private:
    std::string m_title;
    
    bool m_resizable = false;
    bool m_draggable = false;
    bool m_closable = false;
    bool m_collapsible = false;
    bool m_collapsed = false;
    
    bool m_isDragging = false;
    float m_dragOffsetX = 0.0f;
    float m_dragOffsetY = 0.0f;
    
    CloseCallback m_closeCallback;
};

/**
 * Input component for text entry
 */
class UIInput : public UIElement {
public:
    UIInput();
    
    // Text
    void setText(const std::string& text) { m_text = text; }
    const std::string& getText() const { return m_text; }
    
    // Placeholder
    void setPlaceholder(const std::string& placeholder) { m_placeholder = placeholder; }
    const std::string& getPlaceholder() const { return m_placeholder; }
    
    // Input type
    enum class Type {
        Text,
        Password,
        Number,
        Email,
        Search
    };
    
    void setType(Type type) { m_type = type; }
    Type getType() const { return m_type; }
    
    // Max length
    void setMaxLength(int maxLen) { m_maxLength = maxLen; }
    int getMaxLength() const { return m_maxLength; }
    
    // Read-only
    void setReadOnly(bool readOnly) { m_readOnly = readOnly; }
    bool isReadOnly() const { return m_readOnly; }
    
    // Selection
    void setSelectionStart(int start) { m_selectionStart = start; }
    void setSelectionEnd(int end) { m_selectionEnd = end; }
    std::string getSelectedText() const;
    
    // Cursor
    int getCursorPos() const { return m_cursorPos; }
    void setCursorPos(int pos) { m_cursorPos = pos; }

    // Events
    using ChangeCallback = std::function<void(UIInput*, const std::string&)>;
    void setOnChange(ChangeCallback cb) { m_changeCallback = cb; }
    
    using SubmitCallback = std::function<void(UIInput*)>;
    void setOnSubmit(SubmitCallback cb) { m_submitCallback = cb; }

    // Override
    bool handleEvent(const UIEvent& event) override;
    void render(UIContext& context) override;
    void update(float deltaTime) override;

private:
    std::string m_text;
    std::string m_placeholder;
    Type m_type = Type::Text;
    
    int m_maxLength = 0;  // 0 = unlimited
    bool m_readOnly = false;
    
    int m_cursorPos = 0;
    int m_selectionStart = -1;
    int m_selectionEnd = -1;
    
    bool m_showCursor = true;
    float m_cursorBlinkTimer = 0.0f;
    
    int m_scrollOffset = 0;
    
    ChangeCallback m_changeCallback;
    SubmitCallback m_submitCallback;
};

/**
 * Checkbox component
 */
class UICheckbox : public UIElement {
public:
    UICheckbox();
    
    // State
    void setChecked(bool checked) { m_checked = checked; }
    bool isChecked() const { return m_checked; }
    
    // Label
    void setLabel(const std::string& label) { m_label = label; }
    const std::string& getLabel() const { return m_label; }
    
    // Tri-state
    void setTriState(bool triState) { m_triState = triState; }
    bool isTriState() const { return m_triState; }
    
    enum class State {
        Unchecked,
        Checked,
        Indeterminate
    };
    
    State getState() const;

    // Events
    using ChangeCallback = std::function<void(UICheckbox*, bool)>;
    void setOnChange(ChangeCallback cb) { m_changeCallback = cb; }

    // Override
    bool handleEvent(const UIEvent& event) override;
    void render(UIContext& context) override;

private:
    bool m_checked = false;
    std::string m_label;
    bool m_triState = false;
    
    ChangeCallback m_changeCallback;
};

/**
 * Slider component
 */
class UISlider : public UIElement {
public:
    UISlider();
    
    // Range
    void setMin(float min) { m_min = min; }
    float getMin() const { return m_min; }
    
    void setMax(float max) { m_max = max; }
    float getMax() const { return m_max; }
    
    void setValue(float value);
    float getValue() const { return m_value; }
    
    // Step
    void setStep(float step) { m_step = step; }
    float getStep() const { return m_step; }
    
    // Orientation
    enum class Orientation {
        Horizontal,
        Vertical
    };
    
    void setOrientation(Orientation orient) { m_orientation = orient; }
    Orientation getOrientation() const { return m_orientation; }

    // Events
    using ChangeCallback = std::function<void(UISlider*, float)>;
    void setOnChange(ChangeCallback cb) { m_changeCallback = cb; }

    // Override
    bool handleEvent(const UIEvent& event) override;
    void render(UIContext& context) override;

private:
    float m_min = 0.0f;
    float m_max = 100.0f;
    float m_value = 50.0f;
    float m_step = 1.0f;
    
    Orientation m_orientation = Orientation::Horizontal;
    
    bool m_isDragging = false;
    
    ChangeCallback m_changeCallback;
};

} // namespace havel::ui
