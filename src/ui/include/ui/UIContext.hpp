#pragma once

#include <ui/UITypes.hpp>
#include <ui/UIEvent.hpp>
#include <ui/UIElement.hpp>
#include <ui/UIStyle.hpp>
#include <wm/VulkanRenderer.hpp>
#include <memory>
#include <vector>
#include <stack>

namespace havel::ui {

/**
 * UI Render batch for efficient Vulkan rendering
 */
struct UIRenderBatch {
    std::vector<float> vertices;  // x, y, u, v, r, g, b, a
    std::vector<uint32_t> indices;
    uint64_t textureId = 0;
};

/**
 * Main UI Context - manages UI rendering and interaction
 */
class UIContext {
public:
    UIContext();
    ~UIContext();

    // Initialization
    bool initialize(VulkanRenderer* renderer);
    void shutdown();

    // Root element
    void setRoot(std::shared_ptr<UIElement> root) { m_root = root; }
    std::shared_ptr<UIElement> getRoot() const { return m_root; }

    // Rendering
    void beginFrame();
    void render();
    void endFrame();

    // Input handling
    bool handleEvent(const UIEvent& event);
    
    void setMousePosition(float x, float y);
    void setWindowSize(int width, int height);

    // Element management
    UIElement* getElementAt(float x, float y) const;
    void focusElement(UIElement* element);
    void blurElement(UIElement* element);

    // Theme
    void setTheme(const UITheme& theme) { m_theme = theme; }
    const UITheme& getTheme() const { return m_theme; }

    // State
    bool isInitialized() const { return m_initialized; }
    VulkanRenderer* getRenderer() const { return m_renderer; }
    
    float getDeltaTime() const { return m_deltaTime; }
    void setDeltaTime(float dt) { m_deltaTime = dt; }

    // Debug
    void setShowDebugOverlay(bool show) { m_showDebugOverlay = show; }
    bool getShowDebugOverlay() const { return m_showDebugOverlay; }

    // Rendering helpers (public for components)
    void renderRect(const UIRect& rect, const UIColor& color, float cornerRadius = 0.0f);
    void renderText(const std::string& text, float x, float y, const UIColor& color, float size = 14.0f);
    void renderBorder(const UIRect& rect, const UIBorder& border, const UICornerRadius& cornerRadius);

private:
    // Rendering helpers
    void renderElement(UIElement* element);

    void flushBatch();
    void beginBatch(uint64_t textureId);

    // Event helpers
    void dispatchEvent(UIElement* element, const UIEvent& event);
    void updateHoverState(float x, float y);

    VulkanRenderer* m_renderer = nullptr;
    std::shared_ptr<UIElement> m_root;
    UITheme m_theme;
    
    bool m_initialized = false;
    int m_windowWidth = 1920;
    int m_windowHeight = 1080;
    
    float m_deltaTime = 0.0f;
    
    // Input state
    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
    UIElement* m_hoveredElement = nullptr;
    UIElement* m_focusedElement = nullptr;
    UIElement* m_pressedElement = nullptr;
    
    // Render batching
    std::vector<UIRenderBatch> m_batches;
    UIRenderBatch* m_currentBatch = nullptr;
    
    // Debug
    bool m_showDebugOverlay = false;
};

} // namespace havel::ui
