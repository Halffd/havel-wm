#include <wm/Layout.hpp>

namespace havel {

void Layout::arrangeMasterStack(const std::vector<View*>& views,
                                const Rect& availableRect) {
    if (views.empty() || !availableRect.isValid()) {
        return;
    }

    // Filter to only mapped views
    std::vector<View*> mappedViews;
    for (auto* v : views) {
        if (v && v->isMapped()) {
            mappedViews.push_back(v);
        }
    }

    if (mappedViews.empty()) {
        return;
    }

    const int gap = DEFAULT_GAP;
    Rect inner = {
        availableRect.x + gap,
        availableRect.y + gap,
        availableRect.w - 2 * gap,
        availableRect.h - 2 * gap
    };

    if (!inner.isValid()) {
        return;
    }

    size_t n = mappedViews.size();

    if (n == 1) {
        // Single view fills entire space
        auto* view = mappedViews[0];
        view->setGeom(inner.x, inner.y, inner.w, inner.h);
        return;
    }

    // Master-stack layout
    int masterW = (inner.w * MASTER_RATIO_PERCENT) / 100;
    int stackW = inner.w - masterW;

    if (masterW < 1) masterW = 1;
    if (stackW < 1) stackW = 1;

    // Master view (first)
    auto* master = mappedViews[0];
    master->setGeom(inner.x, inner.y, masterW, inner.h);

    // Stack views (remaining)
    if (n > 1) {
        int stackCount = static_cast<int>(n) - 1;
        int slotH = inner.h / stackCount;
        if (slotH < 1) slotH = 1;

        for (size_t i = 1; i < n; ++i) {
            int vy = inner.y + (static_cast<int>(i) - 1) * slotH;
            int vh = (i == n - 1) ? (inner.y + inner.h - vy) : slotH;
            if (vh < 1) vh = 1;

            auto* view = mappedViews[i];
            view->setGeom(inner.x + masterW, vy, stackW, vh);
        }
    }
}

} // namespace havel
