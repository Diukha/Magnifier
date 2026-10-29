#pragma once
#include <algorithm>
#include <cmath>

namespace lens {
struct Rect { int x, y, w, h; };
struct State {
    int width = 640;
    int height = 320;
    float zoom = 2.0f;
    void resize(int dx, int dy) {
        width = std::clamp(width + dx, 160, 2400);
        height = std::clamp(height + dy, 100, 1600);
    }
    void wheel(int delta) {
        zoom = std::clamp(zoom * std::pow(1.2f, delta / 120.0f), 1.0f, 12.0f);
    }
};
inline Rect window(const State& s, Rect monitor, int x, int y) {
    int w = std::min(s.width, monitor.w);
    int h = std::min(s.height, monitor.h);
    return {std::clamp(x - w / 2, monitor.x, monitor.x + monitor.w - w),
            std::clamp(y - h / 2, monitor.y, monitor.y + monitor.h - h), w, h};
}
inline Rect source(Rect output, Rect monitor, int x, int y, float zoom) {
    int w = std::max(1, static_cast<int>(std::ceil(output.w / zoom)));
    int h = std::max(1, static_cast<int>(std::ceil(output.h / zoom)));
    return {std::clamp(x - w / 2, monitor.x, monitor.x + monitor.w - w),
            std::clamp(y - h / 2, monitor.y, monitor.y + monitor.h - h), w, h};
}
}
