#pragma once

#include <wm/Types.hpp>
#include <wm/View.hpp>
#include <vector>
#include <memory>

namespace havel {

/**
 * Workspace manages a collection of views and tiling state.
 */
class Workspace {
public:
    explicit Workspace(uint32_t id);

    uint32_t id() const { return m_id; }

    // View management
    void addView(std::shared_ptr<View> view);
    void removeView(View* view);
    std::vector<std::shared_ptr<View>> views() const;
    std::vector<std::shared_ptr<View>> mappedViews() const;

    // Tiling state
    bool isTilingEnabled() const { return m_tilingEnabled; }
    void setTilingEnabled(bool enabled);

    // Active view (for focus tracking)
    View* activeView() const { return m_activeView; }
    void setActiveView(View* view);

private:
    uint32_t m_id;
    std::vector<std::shared_ptr<View>> m_views;
    bool m_tilingEnabled = true;
    View* m_activeView = nullptr;
};

} // namespace havel
