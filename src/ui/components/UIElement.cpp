#include <ui/UIElement.hpp>
#include <ui/UIContext.hpp>
#include <algorithm>

namespace havel::ui {

UIElement::UIElement() = default;

UIElement::~UIElement() {
    removeAllChildren();
}

void UIElement::addChild(std::shared_ptr<UIElement> child) {
    if (!child) return;
    child->setParent(this);
    m_children.push_back(child);
}

void UIElement::removeChild(UIElement* child) {
    if (!child) return;
    m_children.erase(
        std::remove_if(m_children.begin(), m_children.end(),
            [child](const std::shared_ptr<UIElement>& c) { return c.get() == child; }),
        m_children.end()
    );
}

void UIElement::removeAllChildren() {
    for (auto& child : m_children) {
        child->setParent(nullptr);
    }
    m_children.clear();
}

void UIElement::setPosition(float x, float y) {
    m_rect.x = x;
    m_rect.y = y;
}

void UIElement::setSize(float w, float h) {
    m_rect.w = w;
    m_rect.h = h;
}

void UIElement::setRect(const UIRect& rect) {
    m_rect = rect;
}

UIRect UIElement::getScreenRect() const {
    UIRect screen = m_rect;
    UIElement* parent = m_parent;
    while (parent) {
        screen.x += parent->m_rect.x;
        screen.y += parent->m_rect.y;
        parent = parent->m_parent;
    }
    return screen;
}

void UIElement::applyTheme(const UITheme& theme) {
    // Apply theme defaults if style is not set
    if (m_style.backgroundColor == UIColor::transparent()) {
        m_style.backgroundColor = theme.background;
    }
    if (m_style.textColor == UIColor::white()) {
        m_style.textColor = theme.text;
    }
    if (m_style.font.family == "sans-serif") {
        m_style.font = theme.defaultFont;
    }
}

bool UIElement::hitTest(float x, float y) const {
    if (!m_style.pointerEvents) return false;
    if (!isVisible()) return false;
    return m_rect.contains(x, y);
}

UIElement* UIElement::getElementAt(float x, float y) {
    if (!hitTest(x, y)) return nullptr;
    
    // Check children in reverse order (top to bottom)
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        float localX = x - m_rect.x;
        float localY = y - m_rect.y;
        UIElement* found = (*it)->getElementAt(localX, localY);
        if (found) return found;
    }
    
    return this;
}

bool UIElement::handleEvent(const UIEvent& event) {
    if (!m_enabled) return false;
    
    switch (event.type) {
        case UIEventType::MouseClick:
            if (m_onClick) m_onClick(event);
            return true;
            
        case UIEventType::MouseMove:
            if (m_onHover && m_hovered) m_onHover(event);
            return true;
            
        case UIEventType::Focus:
            if (m_onFocus) m_onFocus(event);
            return true;
            
        default:
            break;
    }
    
    return false;
}

// UIContainer implementation
void UIContainer::render(UIContext& context) {
    // Container just renders children, no visual representation
    for (const auto& child : m_children) {
        if (child->isRenderable()) {
            context.beginFrame();
            child->render(context);
        }
    }
}

} // namespace havel::ui
