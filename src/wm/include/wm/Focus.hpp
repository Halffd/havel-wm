#pragma once

#include <wm/View.hpp>
#include <list>
#include <memory>

namespace havel {

/**
 * Focus manager handles MRU (most recently used) tracking and focus policy.
 */
class FocusManager {
public:
    /**
     * Promote view to front of MRU list.
     */
    void promote(std::shared_ptr<View> view);

    /**
     * Remove view from MRU tracking.
     */
    void remove(View* view);

    /**
     * Get most recently used view.
     */
    std::shared_ptr<View> mru() const;

    /**
     * Get next/previous view in MRU order.
     */
    std::shared_ptr<View> nextMru(View* current, bool backwards = false) const;

    /**
     * Clear all tracking.
     */
    void clear();

private:
    // Front = most recently used
    std::list<std::shared_ptr<View>> m_mruList;
};

} // namespace havel
