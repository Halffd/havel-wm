// Custom Layouts Plugin - Tiling Window Management
// Implements multiple tiling layouts: master-stack, grid, horizontal, vertical

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <cstdio>
#include <vector>
#include <cmath>

namespace havel {

/**
 * Custom Layouts Plugin
 *
 * Implements multiple tiling layouts:
 * - Master-Stack (default): One large master window, others stacked
 * - Horizontal: All windows side by side
 * - Vertical: All windows stacked vertically
 * - Grid: Windows arranged in a grid
 * - Monocle: One window at a time, fullscreen
 *
 * Keybindings:
 * - Meta+T: Master-stack (default)
 * - Meta+H: Horizontal split
 * - Meta+V: Vertical split
 * - Meta+G: Grid layout
 * - Meta+M: Monocle (one window)
 * - Meta+Enter: Swap master with current
 * - Meta+L: Increase master count
 * - Meta+Shift+L: Decrease master count
 */
class CustomLayoutsPlugin : public Plugin {
public:
    const char* name() const override { return "custom_layouts"; }
    const char* version() const override { return "0.2.0"; }

    void init(CompositorAPI* api) override {
        m_api = api;
        m_currentLayout = Layout::MASTER_STACK;
        m_masterCount = 1;
        m_gapSize = 4;
        m_mainRatio = 0.55f;  // Master window gets 55% of space
        printf("[CustomLayoutsPlugin] Initialized (layout=master-stack, master=%d)\n", m_masterCount);
    }

    void fini() override {
        printf("[CustomLayoutsPlugin] Finalized\n");
        m_api = nullptr;
    }

    void loadConfig(const std::string& configPath) override {
        // Would load layout preferences, gaps, ratios
        (void)configPath;
        printf("[CustomLayoutsPlugin] Config loaded\n");
    }

    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_LOGO = 1 << 6;
        constexpr uint32_t MOD_SHIFT = 1 << 1;

        if (!event.pressed) return false;
        if (!(event.modifiers & MOD_LOGO)) return false;

        // Layout switching
        switch (event.keycode) {
            case 20:  // T - Master-stack
                setLayout(Layout::MASTER_STACK);
                return true;

            case 36:  // H - Horizontal
                setLayout(Layout::HORIZONTAL);
                return true;

            case 47:  // V - Vertical
                setLayout(Layout::VERTICAL);
                return true;

            case 43:  // G - Grid
                setLayout(Layout::GRID);
                return true;

            case 50:  // M - Monocle
                setLayout(Layout::MONOCLE);
                return true;

            case 28:  // Enter - Swap master with current
                swapMaster();
                return true;

            case 38:  // L - Increase master count
                if (event.modifiers & MOD_SHIFT) {
                    decreaseMasterCount();
                } else {
                    increaseMasterCount();
                }
                return true;
        }

        return false;
    }

private:
    CompositorAPI* m_api = nullptr;

    enum class Layout {
        MASTER_STACK,  // One large master, others stacked
        HORIZONTAL,    // All windows side by side
        VERTICAL,      // All windows stacked vertically
        GRID,          // Grid arrangement
        MONOCLE        // One window fullscreen
    };

    Layout m_currentLayout;
    int m_masterCount;
    int m_gapSize;
    float m_mainRatio;

    void setLayout(Layout layout) {
        if (m_currentLayout == layout) {
            return;
        }

        m_currentLayout = layout;

        const char* names[] = {"master-stack", "horizontal", "vertical", "grid", "monocle"};
        printf("[CustomLayoutsPlugin] Layout: %s\n", names[static_cast<int>(layout)]);

        applyLayout();
    }

    void increaseMasterCount() {
        m_masterCount++;
        printf("[CustomLayoutsPlugin] Master count: %d\n", m_masterCount);
        applyLayout();
    }

    void decreaseMasterCount() {
        if (m_masterCount > 1) {
            m_masterCount--;
            printf("[CustomLayoutsPlugin] Master count: %d\n", m_masterCount);
            applyLayout();
        }
    }

    void swapMaster() {
        printf("[CustomLayoutsPlugin] Swap master (requires view reordering)\n");
        // In production, would swap focused view with master
        applyLayout();
    }

    void applyLayout() {
        uint32_t ws = m_api->getActiveWorkspace();
        auto views = m_api->getViewsInWorkspace(ws);
        
        if (views.empty()) {
            printf("[CustomLayoutsPlugin] No windows to arrange\n");
            return;
        }

        int outputW = m_api->getOutputWidth();
        int outputH = m_api->getOutputHeight();
        
        // Account for gaps
        int availW = outputW - (m_gapSize * 2);
        int availH = outputH - (m_gapSize * 2);
        int baseX = m_gapSize;
        int baseY = m_gapSize;

        printf("[CustomLayoutsPlugin] Arranging %d windows in %dx%d\n", 
               static_cast<int>(views.size()), outputW, outputH);

        switch (m_currentLayout) {
            case Layout::MASTER_STACK:
                applyMasterStack(views, baseX, baseY, availW, availH);
                break;
            case Layout::HORIZONTAL:
                applyHorizontal(views, baseX, baseY, availW, availH);
                break;
            case Layout::VERTICAL:
                applyVertical(views, baseX, baseY, availW, availH);
                break;
            case Layout::GRID:
                applyGrid(views, baseX, baseY, availW, availH);
                break;
            case Layout::MONOCLE:
                applyMonocle(views, baseX, baseY, availW, availH);
                break;
        }
    }

    void applyMasterStack(const std::vector<View*>& views, int x, int y, int w, int h) {
        if (views.empty()) return;

        int masterCount = std::min(m_masterCount, static_cast<int>(views.size()));
        int stackCount = static_cast<int>(views.size()) - masterCount;

        if (stackCount <= 0) {
            // All windows are masters, divide equally
            int masterW = w / static_cast<int>(views.size());
            for (size_t i = 0; i < views.size(); i++) {
                int mx = x + static_cast<int>(i) * masterW;
                m_api->setViewGeometry(views[i], mx, y, masterW - m_gapSize, h);
            }
            return;
        }

        // Master area width
        int masterW = static_cast<int>(w * m_mainRatio);
        int stackW = w - masterW - m_gapSize;
        int stackH = h / stackCount;

        // Place masters (left side, stacked vertically)
        int masterH = h / masterCount;
        for (int i = 0; i < masterCount; i++) {
            int my = y + i * masterH;
            m_api->setViewGeometry(views[i], x, my, masterW, masterH - m_gapSize);
        }

        // Place stack (right side)
        for (int i = 0; i < stackCount; i++) {
            int sy = y + i * stackH;
            m_api->setViewGeometry(views[masterCount + i], x + masterW + m_gapSize, sy, 
                                   stackW, stackH - m_gapSize);
        }

        printf("[CustomLayoutsPlugin] Master-stack: %d masters, %d stack\n", masterCount, stackCount);
    }

    void applyHorizontal(const std::vector<View*>& views, int x, int y, int w, int h) {
        if (views.empty()) return;

        int windowW = w / static_cast<int>(views.size());
        
        for (size_t i = 0; i < views.size(); i++) {
            int wx = x + static_cast<int>(i) * windowW;
            m_api->setViewGeometry(views[i], wx, y, windowW - m_gapSize, h);
        }

        printf("[CustomLayoutsPlugin] Horizontal: %d windows side by side\n", 
               static_cast<int>(views.size()));
    }

    void applyVertical(const std::vector<View*>& views, int x, int y, int w, int h) {
        if (views.empty()) return;

        int windowH = h / static_cast<int>(views.size());
        
        for (size_t i = 0; i < views.size(); i++) {
            int wy = y + static_cast<int>(i) * windowH;
            m_api->setViewGeometry(views[i], x, wy, w, windowH - m_gapSize);
        }

        printf("[CustomLayoutsPlugin] Vertical: %d windows stacked\n", 
               static_cast<int>(views.size()));
    }

    void applyGrid(const std::vector<View*>& views, int x, int y, int w, int h) {
        if (views.empty()) return;

        // Calculate grid dimensions
        int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(views.size()))));
        int rows = (static_cast<int>(views.size()) + cols - 1) / cols;

        int windowW = w / cols;
        int windowH = h / rows;

        for (size_t i = 0; i < views.size(); i++) {
            int col = static_cast<int>(i) % cols;
            int row = static_cast<int>(i) / cols;
            int wx = x + col * windowW;
            int wy = y + row * windowH;
            m_api->setViewGeometry(views[i], wx, wy, windowW - m_gapSize, windowH - m_gapSize);
        }

        printf("[CustomLayoutsPlugin] Grid: %dx%d (%d windows)\n", cols, rows, 
               static_cast<int>(views.size()));
    }

    void applyMonocle(const std::vector<View*>& views, int x, int y, int w, int h) {
        if (views.empty()) return;

        // Get focused view
        View* focused = m_api->getFocusedView();
        
        for (size_t i = 0; i < views.size(); i++) {
            if (views[i] == focused) {
                // Focused window is fullscreen
                m_api->setViewGeometry(views[i], x, y, w, h);
            } else {
                // Other windows are hidden (positioned off-screen or zero size)
                // In production, would hide or minimize
                m_api->setViewGeometry(views[i], x, y, 1, 1);
            }
        }

        printf("[CustomLayoutsPlugin] Monocle: showing focused window fullscreen\n");
    }
};

// Plugin factory
Plugin* create_custom_layouts_plugin() {
    return new CustomLayoutsPlugin();
}

} // namespace havel
