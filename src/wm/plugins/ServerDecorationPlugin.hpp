#pragma once

#include <wm/plugins/Plugin.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <unordered_map>
#include <string>
#include <array>

namespace havel {

/**
 * Server-side window decoration
 * Provides title bars, borders, and window controls
 */
struct WindowDecoration {
    void* view = nullptr;              // Opaque view pointer
    std::string title;                 // Window title
    bool focused = false;              // Is window focused
    bool maximized = false;            // Is window maximized
    bool fullscreen = false;           // Is window fullscreen
    int x = 0, y = 0;                  // Window position
    int width = 0, height = 0;         // Window size
    uint32_t workspace = 0;            // Workspace ID

    // For maximize restore
    int view_start_x = 0, view_start_y = 0;
    int view_start_w = 0, view_start_h = 0;

    // Decoration dimensions
    static constexpr int TITLE_BAR_HEIGHT = 32;
    static constexpr int BORDER_WIDTH = 1;

    // Colors (inline constexpr to avoid linker errors)
    static inline constexpr std::array<float, 4> FOCUSED_BG   = {0.2f, 0.3f, 0.4f, 1.0f};
    static inline constexpr std::array<float, 4> UNFOCUSED_BG = {0.15f, 0.15f, 0.2f, 1.0f};
    static inline constexpr std::array<float, 4> TEXT_COLOR   = {1.0f, 1.0f, 1.0f, 1.0f};
    static inline constexpr std::array<float, 4> BUTTON_HOVER = {0.4f, 0.4f, 0.5f, 1.0f};
};

/**
 * Button types for window decorations
 */
enum class DecoButton {
    None = 0,
    Close,
    Maximize,
    Minimize
};

/**
 * Server-Side Decoration Plugin
 *
 * Implements server-side window decorations using the
 * xdg_decoration or wlr_server_decoration protocol.
 *
 * Features:
 * - Title bar with window title
 * - Close, maximize, minimize buttons
 * - Visual focus indication
 * - Border around windows
 *
 * Keybindings:
 * - None (decorations are always visible)
 */
class ServerDecorationPlugin : public Plugin {
public:
    const char* name() const override { return "server_decoration"; }
    const char* version() const override { return "0.1.0"; }

    void init(CompositorAPI* api) override;
    void fini() override;
    void loadConfig(const std::string& configPath) override;

    bool onKey(const KeyEvent& event) override;
    void onOutputFrame(const OutputFrameEvent& event) override;
    void onViewMap(const ViewEvent& event) override;
    void onViewUnmap(const ViewEvent& event) override;
    void onViewDestroy(const ViewEvent& event) override;
    void renderOverlay(void* renderer) override;
    void onMouseMotion(int x, int y) override;
    void onMouseButton(uint32_t button, bool pressed, int x, int y) override;

private:
    CompositorAPI* m_api = nullptr;
    std::unordered_map<void*, WindowDecoration> m_decorations;
    void* m_hoveredView = nullptr;           // View under mouse
    DecoButton m_hoveredButton = DecoButton::None;  // Currently hovered button
    int m_mouseX = 0, m_mouseY = 0;          // Last mouse position

    // Decoration manager global (for protocol)
    void* m_decorationManager = nullptr;

    void updateDecoration(void* view, const WindowDecoration& deco);
    void renderTitleBar(OverlayRenderer* renderer, const WindowDecoration& deco);
    void renderBorder(OverlayRenderer* renderer, const WindowDecoration& deco);
    void renderButtons(OverlayRenderer* renderer, const WindowDecoration& deco);

    void handleCloseClick(void* view);
    void handleMaximizeClick(void* view);
    void handleMinimizeClick(void* view);

    DecoButton buttonAtPosition(const WindowDecoration& deco, int mx, int my);
};

// Plugin factory
Plugin* create_server_decoration_plugin();

} // namespace havel
