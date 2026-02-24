#pragma once

#include <ui/UITypes.hpp>
#include <ui/UIEvent.hpp>
#include <ui/UIStyle.hpp>
#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace havel::ui {

// Forward declarations
class UIContext;

/**
 * Base class for all UI elements
 */
class UIElement : public std::enable_shared_from_this<UIElement> {
public:
    UIElement();
    virtual ~UIElement();

    // Identity
    void setId(const std::string& id) { m_id = id; }
    const std::string& getId() const { return m_id; }
    
    void setName(const std::string& name) { m_name = name; }
    const std::string& getName() const { return m_name; }

    // Parent/Children
    void setParent(UIElement* parent) { m_parent = parent; }
    UIElement* getParent() const { return m_parent; }
    
    void addChild(std::shared_ptr<UIElement> child);
    void removeChild(UIElement* child);
    void removeAllChildren();
    
    const std::vector<std::shared_ptr<UIElement>>& getChildren() const { return m_children; }
    
    template<typename T>
    std::shared_ptr<T> findChildById(const std::string& id) const {
        for (const auto& child : m_children) {
            if (child->getId() == id) {
                return std::static_pointer_cast<T>(child);
            }
            auto found = child->findChildById<T>(id);
            if (found) return found;
        }
        return nullptr;
    }

    // Position and Size
    void setPosition(float x, float y);
    void setSize(float w, float h);
    void setRect(const UIRect& rect);
    
    UIRect getRect() const { return m_rect; }
    UIRect getScreenRect() const;
    
    float getX() const { return m_rect.x; }
    float getY() const { return m_rect.y; }
    float getWidth() const { return m_rect.w; }
    float getHeight() const { return m_rect.h; }

    // Style
    void setStyle(const UIStyle& style) { m_style = style; }
    const UIStyle& getStyle() const { return m_style; }
    UIStyle& getStyle() { return m_style; }
    
    void applyTheme(const UITheme& theme);

    // Visibility
    void setVisibility(UIVisibility visibility) { m_visibility = visibility; }
    UIVisibility getVisibility() const { return m_visibility; }
    bool isVisible() const { return m_visibility == UIVisibility::Visible; }
    bool isRenderable() const { return m_visibility != UIVisibility::Collapsed; }

    // State
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    
    void setFocused(bool focused) { m_focused = focused; }
    bool isFocused() const { return m_focused; }
    
    void setHovered(bool hovered) { m_hovered = hovered; }
    bool isHovered() const { return m_hovered; }

    // Hit testing
    virtual bool hitTest(float x, float y) const;
    virtual UIElement* getElementAt(float x, float y);

    // Event handling
    virtual bool handleEvent(const UIEvent& event);
    
    // Event callbacks
    using EventCallback = std::function<void(const UIEvent&)>;
    
    void setOnClick(EventCallback cb) { m_onClick = cb; }
    void setOnHover(EventCallback cb) { m_onHover = cb; }
    void setOnFocus(EventCallback cb) { m_onFocus = cb; }

    // Rendering (called by UIContext)
    virtual void render(UIContext& context) = 0;
    virtual void update(float deltaTime) {}

    // Layout
    virtual void layout() {}
    virtual void invalidateLayout() {}

protected:
    std::string m_id;
    std::string m_name;
    
    UIElement* m_parent = nullptr;
    std::vector<std::shared_ptr<UIElement>> m_children;
    
    UIRect m_rect;
    UIStyle m_style;
    UIVisibility m_visibility = UIVisibility::Visible;
    
    bool m_enabled = true;
    bool m_focused = false;
    bool m_hovered = false;
    
    // Event callbacks
    EventCallback m_onClick;
    EventCallback m_onHover;
    EventCallback m_onFocus;
};

/**
 * Simple container element for grouping
 */
class UIContainer : public UIElement {
public:
    void render(UIContext& context) override;
};

} // namespace havel::ui
