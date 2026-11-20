#pragma once

#include <wm/View.hpp>
#include <list>

namespace havel {

/**
 * Focus manager handles MRU (most recently used) tracking and focus policy.
 * 
 * LIFETIME: Stores NON-OWNING raw pointers.
 * Views are owned by C layer. Call remove() when view is destroyed.
 */
class FocusManager {
public:
    /**
     * Promote view to front of MRU list.
     */
    void promote(View* view);

    /**
     * Remove view from MRU tracking. Call when view is destroyed.
     */
    void remove(View* view);

    /**
     * Get most recently used view.
     */
    View* mru() const;

    /**
     * Get next/previous view in MRU order.
     */
    View* nextMru(View* current, bool backwards = false) const;

    /**
     * Clear all tracking.
     */
    void clear();

private:
    // Front = most recently used. Non-owning raw pointers.
    std::list<View*> m_mruList;
};

} // namespace havel
