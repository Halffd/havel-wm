#include <wm/Focus.hpp>
#include <algorithm>

namespace havel {

void FocusManager::promote(View* view) {
    if (!view) return;

    // Remove if already in list
    remove(view);

    // Insert at front (most recently used)
    m_mruList.push_front(view);
}

void FocusManager::remove(View* view) {
    if (!view) return;

    m_mruList.remove(view);
}

View* FocusManager::mru() const {
    if (m_mruList.empty()) {
        return nullptr;
    }
    return m_mruList.front();
}

View* FocusManager::nextMru(View* current, bool backwards) const {
    if (m_mruList.empty()) {
        return nullptr;
    }

    if (!current) {
        return mru();
    }

    // Find current in list
    auto it = std::find(m_mruList.begin(), m_mruList.end(), current);

    if (it == m_mruList.end()) {
        return mru();
    }

    // Move to next/previous
    if (backwards) {
        ++it;
        if (it == m_mruList.end()) {
            it = m_mruList.begin();
        }
    } else {
        if (it == m_mruList.begin()) {
            it = m_mruList.end();
        }
        --it;
    }

    return *it;
}

void FocusManager::clear() {
    m_mruList.clear();
}

} // namespace havel
