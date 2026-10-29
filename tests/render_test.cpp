// Integration test of the actual capture/presentation pipeline. Creates only
// its own fixture and overlay; never injects input or moves the user's cursor.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <dwmapi.h>
#include <iostream>
#include <stdexcept>
#include "../renderer.h"

namespace {
constexpr COLORREF a = RGB(23, 81, 137), b = RGB(219, 177, 93);
COLORREF tile(int x, int y) { return ((x/16+y/16)%2) ? a : b; }
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
LRESULT CALLBACK Fixture(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(window, &ps);
        RECT bounds{}; GetClientRect(window, &bounds);
        HBRUSH brushes[] = {CreateSolidBrush(b), CreateSolidBrush(a)};
        for (int y=0;y<bounds.bottom;y+=16) for (int x=0;x<bounds.right;x+=16) {
            RECT r{x,y,std::min(x+16,int(bounds.right)),std::min(y+16,int(bounds.bottom))};
            FillRect(dc,&r,brushes[(x/16+y/16)%2]);
        }
        DeleteObject(brushes[0]); DeleteObject(brushes[1]);
        EndPaint(window,&ps); return 0;
    }
    return DefWindowProcW(window,msg,wp,lp);
}
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW cls{}; cls.hInstance=instance; cls.lpfnWndProc=Fixture;
    cls.lpszClassName=L"MouseLens.RenderTest.Fixture"; RegisterClassW(&cls);
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    const int x=int(work.left)+40,y=int(work.top)+40;
    HWND fixture=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,cls.lpszClassName,
        L"MouseLens render test",WS_POPUP,x,y,720,440,nullptr,nullptr,instance,nullptr);
    HWND overlay=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOPMOST|WS_EX_TOOLWINDOW,
        L"STATIC",L"MouseLens test overlay",WS_POPUP,x,y,400,200,nullptr,nullptr,instance,nullptr);
    int result=0;
    try {
        require(fixture && overlay,"Window creation failed");
        require(SetWindowDisplayAffinity(overlay,WDA_EXCLUDEFROMCAPTURE),"Capture exclusion failed");
        ShowWindow(fixture,SW_SHOWNOACTIVATE); UpdateWindow(fixture); DwmFlush();
        DWORD affinity=0;
        require(GetWindowDisplayAffinity(overlay,&affinity) && affinity==WDA_EXCLUDEFROMCAPTURE,
                "Capture exclusion not applied");
        const DWORD initialObjects=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        {
            lens::Renderer renderer;
            for (int i=0;i<120;++i) {
                lens::Rect destination{x+20+(i*7)%220,y+20+(i*3)%160,360+(i%3)*20,200+(i%2)*20};
                const int sw=80+i%45,sh=50+i%25;
                lens::Rect source{destination.x+destination.w/2-sw/2,
                                  destination.y+destination.h/2-sh/2,sw,sh};
                lens::Color color{170,23,91,static_cast<unsigned char>((i%3)*127+(i%3==2))};
                require(renderer.render(overlay,destination,source,color),"Capture/present failed");
                ShowWindow(overlay,SW_SHOWNOACTIVATE);
                DwmFlush();
                // Recapture with the magnified window overlapping the source:
                // every captured pixel must still match the underlying fixture.
                require(renderer.render(overlay,destination,source,color),"Recapture failed");
                const auto& captured=renderer.captured();
                for(int row=0;row<sh;++row) for(int col=0;col<sw;++col) {
                    const COLORREF expected=tile(source.x-x+col,source.y-y+row);
                    const auto rgb=(std::uint32_t(GetRValue(expected))<<16)|
                                   (std::uint32_t(GetGValue(expected))<<8)|GetBValue(expected);
                    require((captured.pixels[row*sw+col]&0xFFFFFFu)==rgb,
                            "Self-capture or stale/incorrect source pixels detected");
                }
                const auto& composed=renderer.composed();
                require(composed.pixels[0]==lens::premultiplied(color),"Border alpha/color mismatch");
                require((composed.pixels[4*destination.w+4]>>24)==255,"Lens interior not opaque");
                RECT actual{}; GetWindowRect(overlay,&actual);
                require(actual.left==destination.x && actual.top==destination.y &&
                        actual.right-actual.left==destination.w && actual.bottom-actual.top==destination.h,
                        "Frame geometry not committed with image");
            }
        }
        require(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=initialObjects+1,"GDI objects leaked");
        std::cout << "PASS: 120 moving/resizing frames, overlapping recapture, every source pixel, "
                     "border alpha 0/127/255, opaque interior, geometry and GDI cleanup\n";
    } catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; result=1; }
    if(overlay) DestroyWindow(overlay);
    if(fixture) DestroyWindow(fixture);
    return result;
}
