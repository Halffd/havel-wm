#pragma once

#include <wm/Types.hpp>
#include <wm/View.hpp>
#include <vector>

namespace havel {

/**
 * Layout engine handles tiling arrangement of views.
 */
class Layout {
public:
    /**
     * Arrange views in master-stack layout.
     * @param views List of views to arrange (raw pointers, non-owning)
     * @param availableRect Available screen space (after gaps)
     */
    static void arrangeMasterStack(const std::vector<View*>& views,
                                   const Rect& availableRect);

    // Declarative configuration: gap between tiled windows and the edges.
    static void setGaps(int px) { s_gaps = px < 0 ? 0 : px; }
    static int gaps() { return s_gaps; }

private:
    static int s_gaps;
    static constexpr int MASTER_RATIO_PERCENT = 60;
};

} // namespace havel
