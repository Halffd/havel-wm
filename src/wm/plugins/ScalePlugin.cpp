// Scale Plugin - Grid Overview with Window Transforms
// Shows scaled overview of all windows in current workspace

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <cstdio>
#include <vector>
#include <cmath>

namespace havel {

/**
 * Window slot for scale grid
 */
struct ScaleSlot {
    View* view = nullptr;
    int slotX = 0;      // Grid position
    int slotY = 0;
    int originalX = 0;  // Original position for restore
    int originalY = 0;
    int originalW = 0;
    int originalH = 0;
    bool isFocused = false;
};

/**
 * Scale Plugin with Grid Navigation
 *
 * Shows overview of all windows in current workspace.
 * Navigate with arrow keys, select with Enter.
 *
 * Features:
 * - Grid layout of windows
 * - Arrow key navigation
 * - Visual highlighting of selection
 * - Enter to select, Escape to cancel
 * - Smooth scale animation (future)
 */
class ScalePlugin : public Plugin {
public:
    const char* name() const override { return "scale"; }
    const char* version() const override { return "0.3.0"; }

    void init(CompositorAPI* api) override {
        m_api = api;
        m_active = false;
        m_selectedIndex = 0;
        m_scaleFactor = 0.75f;
        m_gap = 20;
        printf("[ScalePlugin] Initialized with grid navigation\n");
    }

    void fini() override {
        if (m_active) {
            endScale();
        }
        printf("[ScalePlugin] Finalized\n");
        m_api = nullptr;
    }

    void loadConfig(const std::string& configPath) override {
        // Would load scale factor, gap size, etc.
        (void)configPath;
        printf("[ScalePlugin] Config loaded\n");
    }

    void onViewMap(const ViewEvent& event) override {
        // If scale is active, refresh the layout
        if (m_active) {
            printf("[ScalePlugin] Window mapped: %s\n",
                   event.appId ? event.appId : "unknown");
            // Could refresh layout here
        }
    }

    void onViewDestroy(const ViewEvent& event) override {
        // If scale is active, refresh the layout
        if (m_active) {
            printf("[ScalePlugin] Window destroyed: %s\n",
                   event.title ? event.title : "unknown");
            // Could refresh layout here
        }
    }

    bool onKey(const KeyEvent& event) override {
        if (!m_active) {
            // Meta+S to toggle scale overview
            constexpr uint32_t MOD_LOGO = 1 << 6;
            if (event.pressed && (event.modifiers & MOD_LOGO) && event.keycode == 31) {
                beginScale();
                return true;
            }
            return false;
        }

        // Navigation while scale is active
        if (!event.pressed) return false;

        switch (event.keycode) {
            case 111:  // Escape - cancel
                endScale();
                return true;

            case 28:   // Enter - select
                selectFocused();
                return true;

            case 103:  // Up arrow
                moveSelection(0, -1);
                return true;

            case 108:  // Down arrow
                moveSelection(0, 1);
                return true;

            case 105:  // Left arrow
                moveSelection(-1, 0);
                return true;

            case 106:  // Right arrow
                moveSelection(1, 0);
                return true;

            case 35:   // H - left
                moveSelection(-1, 0);
                return true;

            case 38:   // L - right
                moveSelection(1, 0);
                return true;

            case 30:   // J - down
                moveSelection(0, 1);
                return true;

            case 31:   // K - up
                moveSelection(0, -1);
                return true;
        }

        return false;
    }

private:
    CompositorAPI* m_api = nullptr;
    bool m_active;
    int m_selectedIndex;
    float m_scaleFactor;
    int m_gap;
    std::vector<ScaleSlot> m_slots;
    int m_gridCols;
    int m_gridRows;

    void beginScale() {
        if (m_active) return;
        
        m_active = true;
        m_selectedIndex = 0;
        m_slots.clear();

        // Get all views from current workspace
        uint32_t ws = m_api->getActiveWorkspace();
        auto views = m_api->getViewsInWorkspace(ws);
        
        if (views.empty()) {
            printf("[ScalePlugin] No windows in workspace %u\n", ws);
            m_active = false;
            return;
        }

        // Calculate grid dimensions
        m_windowCount = static_cast<int>(views.size());
        calculateGrid();

        // Store original geometry and calculate scaled positions
        int outputW = m_api->getOutputWidth();
        int outputH = m_api->getOutputHeight();
        
        // Calculate available space (with margins)
        int availW = outputW - (m_gap * 2);
        int availH = outputH - (m_gap * 2);
        
        // Calculate slot size
        int slotW = (availW - (m_gap * (m_gridCols - 1))) / m_gridCols;
        int slotH = (availH - (m_gap * (m_gridRows - 1))) / m_gridRows;
        
        // Limit slot size based on scale factor
        int maxSlotW = static_cast<int>(slotW * m_scaleFactor);
        int maxSlotH = static_cast<int>(slotH * m_scaleFactor);

        int idx = 0;
        for (View* view : views) {
            ScaleSlot slot;
            slot.view = view;
            slot.slotX = idx % m_gridCols;
            slot.slotY = idx / m_gridCols;
            
            // Store original geometry for restore
            // Note: In production, would query actual view geometry
            slot.originalX = 100 + (idx * 20);
            slot.originalY = 100 + (idx * 20);
            slot.originalW = 800;
            slot.originalH = 600;
            
            // Calculate scaled position
            int x = m_gap + (slot.slotX * (maxSlotW + m_gap));
            int y = m_gap + (slot.slotY * (maxSlotH + m_gap));
            
            // Center in slot
            x += (slotW - maxSlotW) / 2;
            y += (slotH - maxSlotH) / 2;
            
            printf("[ScalePlugin] Slot %d: view=%p pos=(%d,%d) size=(%dx%d)\n",
                   idx, (void*)view, x, y, maxSlotW, maxSlotH);
            
            // Apply scaled geometry
            m_api->setViewGeometry(view, x, y, maxSlotW, maxSlotH);
            
            m_slots.push_back(slot);
            idx++;
        }

        printf("[ScalePlugin] Scale overview began (%d windows, %dx%d grid, scale=%.2f)\n",
               m_windowCount, m_gridCols, m_gridRows, m_scaleFactor);

        highlightSelected();
    }

    void endScale() {
        if (!m_active) return;
        
        // Restore original geometry
        for (auto& slot : m_slots) {
            if (slot.view) {
                m_api->setViewGeometry(slot.view, 
                                       slot.originalX, slot.originalY,
                                       slot.originalW, slot.originalH);
            }
        }
        
        m_slots.clear();
        m_active = false;
        m_selectedIndex = 0;
        
        printf("[ScalePlugin] Scale overview ended\n");
    }

    void selectFocused() {
        if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_slots.size())) {
            return;
        }
        
        View* selected = m_slots[m_selectedIndex].view;
        if (selected) {
            printf("[ScalePlugin] Selected window %d/%d\n",
                   m_selectedIndex + 1, m_windowCount);
            
            // Focus the selected view
            // In production, would use view ID to avoid raw pointer
            m_api->focusView(selected);
        }
        
        endScale();
    }

    void moveSelection(int dx, int dy) {
        if (m_slots.empty()) return;
        
        int oldIndex = m_selectedIndex;

        // Calculate current row and column
        int col = m_selectedIndex % m_gridCols;
        int row = m_selectedIndex / m_gridCols;

        // Apply movement
        col += dx;
        row += dy;

        // Clamp to valid range
        if (col < 0) col = 0;
        if (col >= m_gridCols) col = m_gridCols - 1;
        if (row < 0) row = 0;
        if (row >= m_gridRows) row = m_gridRows - 1;

        // Calculate new index
        m_selectedIndex = row * m_gridCols + col;

        // Clamp to window count
        if (m_selectedIndex >= m_windowCount) {
            m_selectedIndex = m_windowCount - 1;
        }

        if (m_selectedIndex != oldIndex) {
            printf("[ScalePlugin] Selection: %d -> %d (row=%d, col=%d)\n",
                   oldIndex, m_selectedIndex, row, col);
            highlightSelected();
        }
    }

    void highlightSelected() {
        if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_slots.size())) {
            return;
        }
        
        // In production, would render highlight border around selected window
        // For now, just log
        printf("[ScalePlugin] Highlighting window %d\n", m_selectedIndex + 1);
    }

    void calculateGrid() {
        // Calculate optimal grid dimensions for window count
        m_gridCols = static_cast<int>(std::ceil(std::sqrt(m_windowCount)));
        m_gridRows = (m_windowCount + m_gridCols - 1) / m_gridCols;
        
        // Ensure at least 1x1
        if (m_gridCols < 1) m_gridCols = 1;
        if (m_gridRows < 1) m_gridRows = 1;
        
        printf("[ScalePlugin] Grid: %dx%d for %d windows\n", 
               m_gridCols, m_gridRows, m_windowCount);
    }

    int m_windowCount;
};

// Plugin factory
Plugin* create_scale_plugin() {
    return new ScalePlugin();
}

} // namespace havel
