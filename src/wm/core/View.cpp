#include <wm/View.hpp>

namespace havel {

View::View() = default;

void View::setWorkspaceId(uint32_t id) {
    if (id < WORKSPACE_COUNT) {
        m_workspaceId = id;
    }
}

void View::setGeom(int x, int y, int w, int h) {
    m_geom.x = x;
    m_geom.y = y;
    m_geom.w = w;
    m_geom.h = h;
}

void View::move(int x, int y) {
    m_geom.x = x;
    m_geom.y = y;
}

void View::resize(int w, int h) {
    if (w > 0) m_geom.w = w;
    if (h > 0) m_geom.h = h;
}

void View::setFloating(bool floating) {
    m_floating = floating;
}

void View::setFloatGeom(const Rect& rect) {
    m_floatGeom = rect;
    m_haveFloatGeom = true;
}

void View::setMapped(bool mapped) {
    m_mapped = mapped;
}

} // namespace havel
