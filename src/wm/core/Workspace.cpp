#include <wm/Workspace.hpp>

namespace havel {

Workspace::Workspace(uint32_t id) : m_id(id) {}

void Workspace::addView(View* view) {
    if (!view) return;
    
    // Check if already tracked
    for (auto* v : m_views) {
        if (v == view) return;
    }
    
    m_views.push_back(view);
}

void Workspace::removeView(View* view) {
    if (!view) return;
    
    m_views.erase(
        std::remove(m_views.begin(), m_views.end(), view),
        m_views.end()
    );
    
    if (m_activeView == view) {
        m_activeView = nullptr;
    }
}

std::vector<View*> Workspace::views() const {
    return m_views;
}

std::vector<View*> Workspace::mappedViews() const {
    std::vector<View*> result;
    for (auto* v : m_views) {
        if (v && v->isMapped()) {
            result.push_back(v);
        }
    }
    return result;
}

std::vector<View*> Workspace::tiledViews() const {
    // Return only mapped, non-floating views for tiling
    std::vector<View*> result;
    for (auto* v : m_views) {
        if (v && v->isMapped() && !v->isFloating()) {
            result.push_back(v);
        }
    }
    return result;
}

void Workspace::setTilingEnabled(bool enabled) {
    m_tilingEnabled = enabled;
}

void Workspace::setActiveView(View* view) {
    m_activeView = view;
}

} // namespace havel
