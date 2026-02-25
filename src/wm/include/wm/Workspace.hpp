#pragma once

#include <wm/Types.hpp>
#include <wm/View.hpp>
#include <vector>
#include <algorithm>

namespace havel {

/**
 * Workspace manages a collection of views and tiling state.
 * 
 * LIFETIME: Views are owned by C layer (wlr_bridge.c).
 * Workspace stores NON-OWNING raw pointers.
 * Never delete views through Workspace.
 */
class Workspace {
public:
    explicit Workspace(uint32_t id);

    uint32_t id() const { return m_id; }

    // View management - raw pointers, C owns lifetime
    void addView(View* view);
    void removeView(View* view);
    std::vector<View*> views() const;
    std::vector<View*> mappedViews() const;
    std::vector<View*> tiledViews() const;  // Exclude floating views

    // Tiling state
    bool isTilingEnabled() const { return m_tilingEnabled; }
    void setTilingEnabled(bool enabled);

    // Active view (for focus tracking)
    View* activeView() const { return m_activeView; }
    void setActiveView(View* view);

private:
    uint32_t m_id;
    std::vector<View*> m_views;  // Non-owning raw pointers
    bool m_tilingEnabled = true;
    View* m_activeView = nullptr;
};

} // namespace havel
