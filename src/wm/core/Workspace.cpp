#include <wm/Workspace.hpp>
#include <algorithm>

namespace havel {

Workspace::Workspace(uint32_t id) : m_id(id) {}

void Workspace::addView(std::shared_ptr<View> view) {
    if (!view) return;
    
    // Check if already tracked
    for (const auto& v : m_views) {
        if (v.get() == view.get()) return;
    }
    
    m_views.push_back(view);
}

void Workspace::removeView(View* view) {
    if (!view) return;
    
    m_views.erase(
        std::remove_if(m_views.begin(), m_views.end(),
            [view](const std::shared_ptr<View>& v) { return v.get() == view; }),
        m_views.end()
    );
    
    if (m_activeView == view) {
        m_activeView = nullptr;
    }
}

std::vector<std::shared_ptr<View>> Workspace::views() const {
    return m_views;
}

std::vector<std::shared_ptr<View>> Workspace::mappedViews() const {
    std::vector<std::shared_ptr<View>> result;
    for (const auto& v : m_views) {
        if (v->isMapped()) {
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
