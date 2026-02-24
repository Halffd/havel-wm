#pragma once

#include <cstdint>

namespace havel {

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool isValid() const { return w > 0 && h > 0; }
};

constexpr uint32_t WORKSPACE_COUNT = 10;
constexpr uint32_t INVALID_WORKSPACE = UINT32_MAX;

} // namespace havel
