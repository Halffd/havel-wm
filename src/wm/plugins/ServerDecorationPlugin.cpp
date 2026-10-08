// Server-Side Window Decoration Plugin
// Provides title bars, borders, and window controls

#include "ServerDecorationPlugin.hpp"
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/bridge.h>
#include <cstdio>
#include <cmath>

namespace havel {

void ServerDecorationPlugin::init(CompositorAPI* api) {
    m_api = api;
    printf("[ServerDecoration] Initialized\n");
}

void ServerDecorationPlugin::fini() {
    m_decorations.clear();
    printf("[ServerDecoration] Finalized\n");
    m_api = nullptr;
}

void ServerDecorationPlugin::loadConfig(const std::string& configPath) {
    (void)configPath;
    printf("[ServerDecoration] Config loaded\n");
}

bool ServerDecorationPlugin::onKey(const KeyEvent& event) {
    (void)event;
    return false;  // No keybindings for decorations
}

void ServerDecorationPlugin::onOutputFrame(const OutputFrameEvent& event) {
    (void)event;
    // Frame updates handled by renderOverlay
}

void ServerDecorationPlugin::onMouseMotion(int mx, int my) {
    m_mouseX = mx;
    m_mouseY = my;
    m_hoveredView = nullptr;
    m_hoveredButton = DecoButton::None;

    // Find which window and button (if any) is under mouse
    for (auto& [viewPtr, deco] : m_decorations) {
        if (deco.fullscreen) continue;

        // Check if mouse is in title bar area
        if (my >= deco.y - WindowDecoration::TITLE_BAR_HEIGHT &&
            my <= deco.y &&
            mx >= deco.x &&
            mx <= deco.x + deco.width) {

            m_hoveredView = viewPtr;
            m_hoveredButton = buttonAtPosition(deco, mx, my);
            break;  // Topmost window
        }
    }

    m_api->scheduleRedraw();
}

void ServerDecorationPlugin::onMouseButton(uint32_t button, bool pressed, int mx, int my) {
    if (!pressed || button != 0x110) return;  // Only handle left button press

    onMouseMotion(mx, my);  // Update hover state

    if (m_hoveredView && m_hoveredButton != DecoButton::None) {
        switch (m_hoveredButton) {
            case DecoButton::Close:
                handleCloseClick(m_hoveredView);
                break;
            case DecoButton::Maximize:
                handleMaximizeClick(m_hoveredView);
                break;
            case DecoButton::Minimize:
                handleMinimizeClick(m_hoveredView);
                break;
            default:
                break;
        }
    }
}

void ServerDecorationPlugin::onViewMap(const ViewEvent& event) {
    if (!event.view) return;

    WindowDecoration deco;
    deco.view = event.view;
    deco.title = event.title ? event.title : "Untitled";
    deco.x = event.x;
    deco.y = event.y;
    deco.width = event.width;
    deco.height = event.height;
    deco.workspace = event.workspace;
    deco.focused = true;
    deco.maximized = false;
    deco.minimized = false;
    deco.fullscreen = false;

    m_decorations[event.view] = deco;
    printf("[ServerDecoration] Window mapped: %s (%p)\n", deco.title.c_str(), event.view);
    
    m_api->scheduleRedraw();
}

void ServerDecorationPlugin::onViewUnmap(const ViewEvent& event) {
    if (!event.view) return;
    
    auto it = m_decorations.find(event.view);
    if (it != m_decorations.end()) {
        printf("[ServerDecoration] Window unmapped: %s\n", it->second.title.c_str());
        // Keep decoration info in case window remaps
    }
    
    m_api->scheduleRedraw();
}

void ServerDecorationPlugin::onViewDestroy(const ViewEvent& event) {
    if (!event.view) return;
    
    auto it = m_decorations.find(event.view);
    if (it != m_decorations.end()) {
        printf("[ServerDecoration] Window destroyed: %s\n", it->second.title.c_str());
        m_decorations.erase(it);
    }
    
    m_api->scheduleRedraw();
}

void ServerDecorationPlugin::renderOverlay(void* rendererPtr) {
    if (!rendererPtr) return;
    OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);

    // Update focus state (part of the redraw signature below)
    for (auto& [viewPtr, deco] : m_decorations) {
        deco.focused = (viewPtr == m_api->getFocusedView());
    }

    // No windows: drop the persistent tree entirely - nothing to show,
    // and a stale tree would keep showing vanished decorations.
    if (m_decorations.empty()) {
        if (m_decoTree) {
            renderer->destroyPersistentTree(&m_decoTree);
            m_decoSig = ~0ull;
        }
        return;
    }

    // Decorations are state-static (geometry/focus/title). Redrawing
    // them per frame re-damages the output every frame and spins the
    // render loop at full tilt: shadows + 4x32-segment corner circles +
    // borders + title bars + buttons = ~150 scene nodes per window per
    // frame, recreated and destroyed forever (observed live: the
    // single-threaded loop stayed CPU-bound in wlroots scene ops and
    // starved IPC and input). Draw once per state change instead.
    const uint64_t sig = decorationSignature();
    if (m_decoTree && sig == m_decoSig) {
        return;  // nothing changed: no redraw, no damage
    }
    m_decoSig = sig;

    if (m_decoTree) {
        renderer->destroyPersistentTree(&m_decoTree);
    }
    m_decoTree = renderer->createPersistentTree();
    if (!m_decoTree) return;

    renderer->setTargetTree(m_decoTree);
    // Render decorations for all non-minimized windows
    for (auto& [viewPtr, deco] : m_decorations) {
        if (deco.minimized) continue;  // No decorations for minimized windows

        // Render shadow first (behind window)
        renderShadow(renderer, deco);

        // Render rounded corners
        renderRoundedCorners(renderer, deco);

        // Render border
        renderBorder(renderer, deco);

        // Render title bar
        renderTitleBar(renderer, deco);

        // Render buttons
        renderButtons(renderer, deco);
    }
    renderer->setTargetTree(nullptr);
}

uint64_t ServerDecorationPlugin::decorationSignature() const {
    // Order-independent mix of every visual input the drawing uses:
    // window set, geometry, focus/state flags, titles, hover state.
    uint64_t h = m_decorations.size() * 0x9e3779b97f4a7c15ull;
    for (const auto& [viewPtr, deco] : m_decorations) {
        h ^= (uint64_t)viewPtr;
        h = h * 131 + (uint64_t)(int64_t)deco.x;
        h = h * 131 + (uint64_t)(int64_t)deco.y;
        h = h * 131 + (uint64_t)(int64_t)deco.width;
        h = h * 131 + (uint64_t)(int64_t)deco.height;
        h = h * 131 + (uint64_t)deco.view;  // view id (marker pos/color inputs)
        h = h * 5 + (deco.focused ? 1 : 0) + (deco.minimized ? 2 : 0)
            + (deco.fullscreen ? 4 : 0) + (deco.maximized ? 8 : 0);
        h ^= std::hash<std::string>{}(deco.title);
    }
    h ^= (uint64_t)m_hoveredView;
    h = h * 3 + (uint64_t)m_hoveredButton;
    return h;
}

void ServerDecorationPlugin::renderShadow(OverlayRenderer* renderer, const WindowDecoration& deco) {
    if (!renderer) return;
    
    // Skip shadow for fullscreen or maximized windows
    if (deco.fullscreen || deco.maximized) return;
    
    // Draw drop shadow behind window
    // Multiple passes with increasing offset for pseudo-blur effect
    const int shadowSize = WindowDecoration::SHADOW_BLUR;
    const int offsetX = WindowDecoration::SHADOW_OFFSET_X;
    const int offsetY = WindowDecoration::SHADOW_OFFSET_Y;
    
    // Draw shadow rectangles (simplified blur by drawing multiple offset passes)
    for (int i = 1; i <= 3; i++) {
        float alpha = WindowDecoration::SHADOW_COLOR[3] / (float)i;
        Color shadowColor(
            WindowDecoration::SHADOW_COLOR[0],
            WindowDecoration::SHADOW_COLOR[1],
            WindowDecoration::SHADOW_COLOR[2],
            alpha
        );
        
        int passOffset = shadowSize / (4 - i);
        
        // Shadow rect behind window
        renderer->drawRect(
            (float)(deco.x + offsetX + passOffset),
            (float)(deco.y + offsetY + passOffset),
            (float)deco.width,
            (float)deco.height,
            shadowColor
        );
    }
}

void ServerDecorationPlugin::renderRoundedCorners(OverlayRenderer* renderer, const WindowDecoration& deco) {
    if (!renderer) return;
    
    // Skip for fullscreen or maximized windows
    if (deco.fullscreen || deco.maximized) return;
    
    const int radius = WindowDecoration::CORNER_RADIUS;
    const auto& color = deco.focused ? WindowDecoration::FOCUSED_BG : WindowDecoration::UNFOCUSED_BG;
    Color cornerColor(color[0], color[1], color[2], color[3]);
    
    // Draw corner circles to create rounded effect
    // Top-left
    renderer->drawCircle(
        (float)(deco.x + radius),
        (float)(deco.y + radius),
        (float)radius,
        cornerColor
    );
    
    // Top-right
    renderer->drawCircle(
        (float)(deco.x + deco.width - radius),
        (float)(deco.y + radius),
        (float)radius,
        cornerColor
    );
    
    // Bottom-left
    renderer->drawCircle(
        (float)(deco.x + radius),
        (float)(deco.y + deco.height - radius),
        (float)radius,
        cornerColor
    );
    
    // Bottom-right
    renderer->drawCircle(
        (float)(deco.x + deco.width - radius),
        (float)(deco.y + deco.height - radius),
        (float)radius,
        cornerColor
    );
}

void ServerDecorationPlugin::renderBorder(OverlayRenderer* renderer, const WindowDecoration& deco) {
    if (!renderer) return;

    const auto& color = deco.focused ? WindowDecoration::FOCUSED_BG : WindowDecoration::UNFOCUSED_BG;

    // Top border (title bar background extends to full border)
    renderer->drawRect(
        (float)deco.x - WindowDecoration::BORDER_WIDTH,
        (float)deco.y - WindowDecoration::BORDER_WIDTH,
        (float)deco.width + WindowDecoration::BORDER_WIDTH * 2,
        (float)WindowDecoration::BORDER_WIDTH,
        Color(color[0], color[1], color[2], color[3])
    );

    // Left border
    renderer->drawRect(
        (float)deco.x - WindowDecoration::BORDER_WIDTH,
        (float)deco.y,
        (float)WindowDecoration::BORDER_WIDTH,
        (float)deco.height,
        Color(color[0], color[1], color[2], color[3])
    );

    // Right border
    renderer->drawRect(
        (float)deco.x + (float)deco.width,
        (float)deco.y,
        (float)WindowDecoration::BORDER_WIDTH,
        (float)deco.height,
        Color(color[0], color[1], color[2], color[3])
    );

    // Bottom border
    renderer->drawRect(
        (float)deco.x - WindowDecoration::BORDER_WIDTH,
        (float)deco.y + (float)deco.height,
        (float)deco.width + WindowDecoration::BORDER_WIDTH * 2,
        (float)WindowDecoration::BORDER_WIDTH,
        Color(color[0], color[1], color[2], color[3])
    );
}

void ServerDecorationPlugin::renderTitleBar(OverlayRenderer* renderer, const WindowDecoration& deco) {
    if (!renderer) return;

    const auto& color = deco.focused ? WindowDecoration::FOCUSED_BG : WindowDecoration::UNFOCUSED_BG;

    // Title bar background
    renderer->drawRect(
        (float)deco.x,
        (float)deco.y - (float)WindowDecoration::TITLE_BAR_HEIGHT,
        (float)deco.width,
        (float)WindowDecoration::TITLE_BAR_HEIGHT,
        Color(color[0], color[1], color[2], color[3])
    );

    // Window title text
    float textX = (float)deco.x + 10.0f;
    float textY = (float)deco.y - (float)WindowDecoration::TITLE_BAR_HEIGHT + 20.0f;

    renderer->drawText(
        deco.title.c_str(),
        textX,
        textY,
        14.0f,
        Color(WindowDecoration::TEXT_COLOR[0], WindowDecoration::TEXT_COLOR[1],
              WindowDecoration::TEXT_COLOR[2], WindowDecoration::TEXT_COLOR[3])
    );
}

void ServerDecorationPlugin::renderButtons(OverlayRenderer* renderer, const WindowDecoration& deco) {
    if (!renderer) return;

    const auto& color = deco.focused ? WindowDecoration::FOCUSED_BG : WindowDecoration::UNFOCUSED_BG;
    int btnSize = 20;
    int spacing = 8;
    int btnY = deco.y - WindowDecoration::TITLE_BAR_HEIGHT + 6;
    int btnX = deco.x + deco.width - btnSize - spacing;

    // Close button (rightmost)
    bool closeHover = (m_hoveredView == deco.view && m_hoveredButton == DecoButton::Close);
    renderer->drawRect(
        (float)btnX, (float)btnY, (float)btnSize, (float)btnSize,
        Color(closeHover ? WindowDecoration::BUTTON_HOVER[0] : color[0],
              closeHover ? WindowDecoration::BUTTON_HOVER[1] : color[1],
              closeHover ? WindowDecoration::BUTTON_HOVER[2] : color[2],
              color[3])
    );
    // X symbol
    renderer->drawText("×", (float)(btnX + 5), (float)(btnY + 16), 18.0f,
                       Color(WindowDecoration::TEXT_COLOR[0], WindowDecoration::TEXT_COLOR[1],
                             WindowDecoration::TEXT_COLOR[2], WindowDecoration::TEXT_COLOR[3]));

    // Maximize button
    btnX -= btnSize + spacing;
    bool maxHover = (m_hoveredView == deco.view && m_hoveredButton == DecoButton::Maximize);
    renderer->drawRect(
        (float)btnX, (float)btnY, (float)btnSize, (float)btnSize,
        Color(maxHover ? WindowDecoration::BUTTON_HOVER[0] : color[0],
              maxHover ? WindowDecoration::BUTTON_HOVER[1] : color[1],
              maxHover ? WindowDecoration::BUTTON_HOVER[2] : color[2],
              color[3])
    );
    // Square symbol
    renderer->drawText("□", (float)(btnX + 5), (float)(btnY + 16), 16.0f,
                       Color(WindowDecoration::TEXT_COLOR[0], WindowDecoration::TEXT_COLOR[1],
                             WindowDecoration::TEXT_COLOR[2], WindowDecoration::TEXT_COLOR[3]));

    // Minimize button
    btnX -= btnSize + spacing;
    bool minHover = (m_hoveredView == deco.view && m_hoveredButton == DecoButton::Minimize);
    renderer->drawRect(
        (float)btnX, (float)btnY, (float)btnSize, (float)btnSize,
        Color(minHover ? WindowDecoration::BUTTON_HOVER[0] : color[0],
              minHover ? WindowDecoration::BUTTON_HOVER[1] : color[1],
              minHover ? WindowDecoration::BUTTON_HOVER[2] : color[2],
              color[3])
    );
    // Dash symbol
    renderer->drawText("−", (float)(btnX + 6), (float)(btnY + 16), 18.0f,
                       Color(WindowDecoration::TEXT_COLOR[0], WindowDecoration::TEXT_COLOR[1],
                             WindowDecoration::TEXT_COLOR[2], WindowDecoration::TEXT_COLOR[3]));
}

DecoButton ServerDecorationPlugin::buttonAtPosition(const WindowDecoration& deco, int mx, int my) {
    int btnSize = 20;
    int spacing = 8;
    int btnY = deco.y - WindowDecoration::TITLE_BAR_HEIGHT + 6;
    int btnX = deco.x + deco.width - btnSize - spacing;

    // Check if mouse is in title bar area
    if (my < deco.y - WindowDecoration::TITLE_BAR_HEIGHT || my > deco.y) {
        return DecoButton::None;
    }
    if (mx < deco.x || mx > deco.x + deco.width) {
        return DecoButton::None;
    }

    // Check buttons (right to left)
    if (mx >= btnX && mx <= btnX + btnSize && my >= btnY && my <= btnY + btnSize) {
        return DecoButton::Close;  // Close
    }

    btnX -= btnSize + spacing;
    if (mx >= btnX && mx <= btnX + btnSize && my >= btnY && my <= btnY + btnSize) {
        return DecoButton::Maximize;  // Maximize
    }

    btnX -= btnSize + spacing;
    if (mx >= btnX && mx <= btnX + btnSize && my >= btnY && my <= btnY + btnSize) {
        return DecoButton::Minimize;  // Minimize
    }

    return DecoButton::None;
}

void ServerDecorationPlugin::handleCloseClick(void* view) {
    if (!view || !m_api) return;
    printf("[ServerDecoration] Close button clicked for window %p\n", view);
    m_api->closeView(static_cast<View*>(view));
}

void ServerDecorationPlugin::handleMaximizeClick(void* view) {
    if (!view || !m_api) return;
    printf("[ServerDecoration] Maximize button clicked for window %p\n", view);
    // Toggle maximize through the CompositorAPI (real window state), then
    // mirror the flag in decoration state for rendering.
    m_api->toggleMaximizeView(static_cast<View*>(view));
    auto it = m_decorations.find(view);
    if (it != m_decorations.end()) {
        it->second.maximized = !it->second.maximized;
    }
    m_api->scheduleRedraw();
}

void ServerDecorationPlugin::handleMinimizeClick(void* view) {
    if (!view || !m_api) return;
    printf("[ServerDecoration] Minimize button clicked for window %p\n", view);

    // Minimize through the CompositorAPI (routes via the view's
    // nativeHandle). Previously this passed the C++ View* directly to
    // the C bridge, which expects the C-side wrapper struct.
    m_api->minimizeView(static_cast<View*>(view));

    // Update decoration state
    auto it = m_decorations.find(view);
    if (it != m_decorations.end()) {
        it->second.minimized = true;
    }

    m_api->scheduleRedraw();
}

// Plugin factory
Plugin* create_server_decoration_plugin() {
    return new ServerDecorationPlugin();
}

} // namespace havel
