#pragma once

#include <wm/Types.hpp>
#include <wm/View.hpp>
#include <memory>
#include <vector>

namespace havel {

/**
 * Layout engine handles tiling arrangement of views.
 */
class Layout {
public:
    /**
     * Arrange views in master-stack layout.
     * @param views List of views to arrange
     * @param availableRect Available screen space (after gaps)
     */
    static void arrangeMasterStack(const std::vector<std::shared_ptr<View>>& views,
                                   const Rect& availableRect);

private:
    static constexpr int DEFAULT_GAP = 10;
    static constexpr int MASTER_RATIO_PERCENT = 60;
};

} // namespace havel
