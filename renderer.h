#pragma once
#include <windows.h>
#include <cstdint>
#include "geometry.h"
#include "color.h"

namespace lens {
constexpr int kBorder = 3;
// GDI surfaces are retained between frames. No partially rendered frame is
// submitted to DWM, and the live HWND never moves before its pixels are ready.
class Dib {
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ original_ = nullptr;
public:
    HDC dc = CreateCompatibleDC(nullptr);
    std::uint32_t* pixels = nullptr;
    int width = 0, height = 0;
    Dib() = default;
    Dib(const Dib&) = delete;
    Dib& operator=(const Dib&) = delete;
    ~Dib() {
        if (original_) SelectObject(dc, original_);
        if (bitmap_) DeleteObject(bitmap_);
        if (dc) DeleteDC(dc);
    }
    bool ensure(int w, int h) {
        if (!dc || w <= 0 || h <= 0) return false;
        if (w == width && h == height) return true;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* data = nullptr;
        HBITMAP next = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        if (!next) return false;
        HGDIOBJ previous = SelectObject(dc, next);
        if (!previous || previous == HGDI_ERROR) { DeleteObject(next); return false; }
        if (!original_) original_ = previous;
        if (bitmap_) DeleteObject(bitmap_);
        bitmap_ = next;
        pixels = static_cast<std::uint32_t*>(data);
        width = w; height = h;
        return true;
    }
};

inline std::uint32_t premultiplied(Color color) {
    const auto channel = [=](unsigned value) { return (value * color.a + 127) / 255; };
    return (std::uint32_t(color.a) << 24) | (channel(color.r) << 16) |
           (channel(color.g) << 8) | channel(color.b);
}

class Renderer {
    Dib source_, output_;
public:
    // SRCCOPY does not draw the system cursor. No ShowCursor/global state is
    // changed. CAPTUREBLT is deliberately omitted; the overlay is also excluded
    // explicitly with WDA_EXCLUDEFROMCAPTURE before it is first shown.
    bool render(HWND window, Rect destination, Rect source, Color border) {
        if (destination.w <= 2*kBorder || destination.h <= 2*kBorder ||
            !source_.ensure(source.w, source.h) ||
            !output_.ensure(destination.w, destination.h)) return false;
        HDC screen = GetDC(nullptr);
        if (!screen) return false;
        BOOL captured = BitBlt(source_.dc, 0, 0, source.w, source.h, screen,
                               source.x, source.y, SRCCOPY | NOMIRRORBITMAP);
        ReleaseDC(nullptr, screen);
        if (!captured) return false;
        SetStretchBltMode(output_.dc, HALFTONE);
        SetBrushOrgEx(output_.dc, 0, 0, nullptr);
        if (!StretchBlt(output_.dc, kBorder, kBorder,
                        destination.w-2*kBorder, destination.h-2*kBorder,
                        source_.dc, 0, 0, source.w, source.h, SRCCOPY)) return false;
        // DIB memory must not be accessed until batched GDI drawing is complete.
        if (!GdiFlush()) return false;
        const auto framePixel = premultiplied(border);
        for (int y=0; y<destination.h; ++y) {
            auto* row = output_.pixels + y*destination.w;
            for (int x=0; x<destination.w; ++x) {
                if (y<kBorder || y>=destination.h-kBorder || x<kBorder || x>=destination.w-kBorder)
                    row[x] = framePixel;
                else row[x] |= 0xFF000000u;
            }
        }
        POINT position{destination.x, destination.y}, origin{};
        SIZE size{destination.w, destination.h};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        // Position, size, magnified image and translucent frame are committed
        // together. No SetWindowPos/InvalidateRect/WM_ERASEBKGND between frames.
        return UpdateLayeredWindow(window, nullptr, &position, &size, output_.dc,
                                   &origin, 0, &blend, ULW_ALPHA) != FALSE;
    }
    const Dib& captured() const { return source_; }
    const Dib& composed() const { return output_; }
};
}
