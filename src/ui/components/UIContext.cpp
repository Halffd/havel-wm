#include <ui/UIContext.hpp>
#include <cstring>

namespace havel::ui {

UIContext::UIContext() = default;

UIContext::~UIContext() {
    shutdown();
}

bool UIContext::initialize(VulkanRenderer* renderer) {
    if (!renderer) return false;
    
    m_renderer = renderer;
    m_initialized = true;
    
    // Create default root element
    m_root = std::make_shared<UIContainer>();
    
    return true;
}

void UIContext::shutdown() {
    m_root.reset();
    m_renderer = nullptr;
    m_initialized = false;
}

void UIContext::beginFrame() {
    if (!m_initialized || !m_renderer) return;
    
    m_batches.clear();
    m_currentBatch = nullptr;
}

void UIContext::render() {
    if (!m_initialized || !m_root) return;
    
    // Update hover state
    updateHoverState(m_mouseX, m_mouseY);
    
    // Render root and children
    renderElement(m_root.get());
    
    // Flush any remaining batch
    flushBatch();
}

void UIContext::endFrame() {
    if (!m_initialized || !m_renderer) return;
    
    // Submit rendered batches to Vulkan
    m_renderer->endFrame();
    m_renderer->submit();
}

void UIContext::renderElement(UIElement* element) {
    if (!element || !element->isRenderable()) return;
    
    // Apply transformations
    UIRect screenRect = element->getScreenRect();
    
    // Render element background
    if (element->getStyle().backgroundColor.a > 0) {
        renderRect(screenRect, element->getStyle().backgroundColor, 
                   element->getStyle().cornerRadius.topLeft);
    }
    
    // Render border
    if (element->getStyle().border.width > 0) {
        renderBorder(screenRect, element->getStyle().border, 
                     element->getStyle().cornerRadius);
    }
    
    // Render children
    for (const auto& child : element->getChildren()) {
        renderElement(child.get());
    }
}

void UIContext::renderRect(const UIRect& rect, const UIColor& color, float cornerRadius) {
    if (!m_renderer) return;
    
    // Normalize coordinates to -1 to 1
    float x0 = (rect.x / m_windowWidth) * 2.0f - 1.0f;
    float y0 = 1.0f - (rect.y / m_windowHeight) * 2.0f;
    float x1 = ((rect.x + rect.w) / m_windowWidth) * 2.0f - 1.0f;
    float y1 = 1.0f - ((rect.y + rect.h) / m_windowHeight) * 2.0f;
    
    // Draw quad using Vulkan renderer
    m_renderer->drawQuad(x0, y0, x1, y1, 0);
}

void UIContext::renderBorder(const UIRect& rect, const UIBorder& border, 
                              const UICornerRadius& cornerRadius) {
    // Top border
    if (border.top > 0) {
        renderRect({rect.x, rect.y, rect.w, border.top}, border.color);
    }
    // Bottom border
    if (border.bottom > 0) {
        renderRect({rect.x, rect.y + rect.h - border.bottom, rect.w, border.bottom}, border.color);
    }
    // Left border
    if (border.left > 0) {
        renderRect({rect.x, rect.y, border.left, rect.h}, border.color);
    }
    // Right border
    if (border.right > 0) {
        renderRect({rect.x + rect.w - border.right, rect.y, border.right, rect.h}, border.color);
    }
}

void UIContext::renderText(const std::string& text, float x, float y, 
                           const UIColor& color, float size) {
    // Text rendering would require font atlas and proper glyph rendering
    // This is a placeholder for future implementation
    (void)text;
    (void)x;
    (void)y;
    (void)color;
    (void)size;
}

void UIContext::flushBatch() {
    if (!m_currentBatch || m_currentBatch->vertices.empty()) return;
    
    // Upload vertices and draw
    // This would use Vulkan buffer uploads and draw calls
    
    m_currentBatch = nullptr;
}

void UIContext::beginBatch(uint64_t textureId) {
    flushBatch();
    
    m_batches.emplace_back();
    m_currentBatch = &m_batches.back();
    m_currentBatch->textureId = textureId;
}

bool UIContext::handleEvent(const UIEvent& event) {
    if (!m_initialized || !m_root) return false;
    
    // Find target element
    UIElement* target = nullptr;
    
    if (event.type == UIEventType::MouseMove || 
        event.type == UIEventType::MouseDown ||
        event.type == UIEventType::MouseUp ||
        event.type == UIEventType::MouseClick) {
        target = getElementAt(event.mouseX, event.mouseY);
    } else if (m_focusedElement) {
        target = m_focusedElement;
    }
    
    if (target) {
        dispatchEvent(target, event);
        return event.handled;
    }
    
    return false;
}

void UIContext::dispatchEvent(UIElement* element, const UIEvent& event) {
    if (!element) return;
    
    // Create local event with element-relative coordinates
    UIEvent localEvent = event;
    
    // Dispatch to element
    if (element->handleEvent(localEvent)) {
        return;  // Event handled, stop propagation
    }
    
    // Bubble up to parent
    if (element->getParent()) {
        dispatchEvent(element->getParent(), localEvent);
    }
}

void UIContext::updateHoverState(float x, float y) {
    UIElement* newHovered = getElementAt(x, y);
    
    if (newHovered != m_hoveredElement) {
        // Mouse leave old element
        if (m_hoveredElement) {
            m_hoveredElement->setHovered(false);
            UIEvent leaveEvent;
            leaveEvent.type = UIEventType::MouseLeave;
            leaveEvent.mouseX = x;
            leaveEvent.mouseY = y;
            dispatchEvent(m_hoveredElement, leaveEvent);
        }
        
        // Mouse enter new element
        if (newHovered) {
            newHovered->setHovered(true);
            UIEvent enterEvent;
            enterEvent.type = UIEventType::MouseEnter;
            enterEvent.mouseX = x;
            enterEvent.mouseY = y;
            dispatchEvent(newHovered, enterEvent);
        }
        
        m_hoveredElement = newHovered;
    }
    
    // Update cursor
    if (newHovered) {
        // Would set cursor based on element style
    }
}

void UIContext::setMousePosition(float x, float y) {
    m_mouseX = x;
    m_mouseY = y;
    updateHoverState(x, y);
}

void UIContext::setWindowSize(int width, int height) {
    m_windowWidth = width;
    m_windowHeight = height;
    
    // Dispatch resize event
    if (m_root) {
        UIEvent resizeEvent;
        resizeEvent.type = UIEventType::Resize;
        dispatchEvent(m_root.get(), resizeEvent);
    }
}

UIElement* UIContext::getElementAt(float x, float y) const {
    if (!m_root) return nullptr;
    return const_cast<UIElement*>(m_root.get())->getElementAt(x, y);
}

void UIContext::focusElement(UIElement* element) {
    if (m_focusedElement && m_focusedElement != element) {
        UIEvent blurEvent;
        blurEvent.type = UIEventType::Blur;
        dispatchEvent(m_focusedElement, blurEvent);
        m_focusedElement->setFocused(false);
    }
    
    m_focusedElement = element;
    
    if (element) {
        element->setFocused(true);
        UIEvent focusEvent;
        focusEvent.type = UIEventType::Focus;
        dispatchEvent(element, focusEvent);
    }
}

void UIContext::blurElement(UIElement* element) {
    if (m_focusedElement == element) {
        UIEvent blurEvent;
        blurEvent.type = UIEventType::Blur;
        dispatchEvent(element, blurEvent);
        element->setFocused(false);
        m_focusedElement = nullptr;
    }
}

} // namespace havel::ui
