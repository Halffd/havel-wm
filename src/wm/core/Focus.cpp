#include <wm/Focus.hpp>
#include <algorithm>

namespace havel {

void FocusManager::promote(std::shared_ptr<View> view) {
    if (!view) return;

    // Remove if already in list
    remove(view.get());

    // Insert at front (most recently used)
    m_mruList.push_front(view);
}

void FocusManager::remove(View* view) {
    if (!view) return;

    m_mruList.remove_if([view](const std::shared_ptr<View>& v) {
        return v.get() == view;
    });
}

std::shared_ptr<View> FocusManager::mru() const {
    if (m_mruList.empty()) {
        return nullptr;
    }
    return m_mruList.front();
}

std::shared_ptr<View> FocusManager::nextMru(View* current, bool backwards) const {
    if (m_mruList.empty()) {
        return nullptr;
    }

    if (!current) {
        return mru();
    }

    // Find current in list
    auto it = std::find_if(m_mruList.begin(), m_mruList.end(),
        [current](const std::shared_ptr<View>& v) { return v.get() == current; });

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
