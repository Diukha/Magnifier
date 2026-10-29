// Integration test of the actual capture/presentation pipeline. Creates only
// its own fixture and overlay; never injects input or moves the user's cursor.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#define LENS_TEST_READBACK
#include <windows.h>
#include <dwmapi.h>
#include <iostream>
#include <stdexcept>
#include "../renderer.h"

namespace {
constexpr COLORREF a = RGB(23, 81, 137), b = RGB(219, 177, 93);
int phase=0;
COLORREF tile(int x, int y) { return ((x/16+y/16+phase)%2) ? a : b; }
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
LRESULT CALLBACK Fixture(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(window, &ps);
        RECT bounds{}; GetClientRect(window, &bounds);
        HBRUSH brushes[] = {CreateSolidBrush(b), CreateSolidBrush(a)};
        for (int y=0;y<bounds.bottom;y+=16) for (int x=0;x<bounds.right;x+=16) {
            RECT r{x,y,std::min(x+16,int(bounds.right)),std::min(y+16,int(bounds.bottom))};
            FillRect(dc,&r,brushes[(x/16+y/16+phase)%2]);
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
    cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
    cls.lpszClassName=L"MouseLens.RenderTest.Fixture"; RegisterClassW(&cls);
    POINT pointer{};GetCursorPos(&pointer);
    MONITORINFO active{};active.cbSize=sizeof(active);
    GetMonitorInfoW(MonitorFromPoint(pointer,MONITOR_DEFAULTTONEAREST),&active);
    const RECT work=active.rcMonitor;
    // Place OUR fixture under the current cursor; never move the user's mouse.
    const int x=std::clamp(int(pointer.x)-360,int(work.left),int(work.right)-720);
    const int y=std::clamp(int(pointer.y)-220,int(work.top),int(work.bottom)-440);
    HWND fixture=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,cls.lpszClassName,
        L"MouseLens render test",WS_POPUP,x,y,720,440,nullptr,nullptr,instance,nullptr);
    HWND overlay=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOPMOST|WS_EX_TOOLWINDOW,
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
                if (i==60 || i==90) {
                    phase=1-phase;
                    InvalidateRect(fixture,nullptr,FALSE);UpdateWindow(fixture);DwmFlush();
                }
                lens::Rect destination{x+20+(i*7)%220,y+20+(i*3)%160,360+(i%3)*20,200+(i%2)*20};
                const int sw=80+i%45,sh=50+i%25;
                lens::Rect source{destination.x+destination.w/2-sw/2,
                                  destination.y+destination.h/2-sh/2,sw,sh};
                if (i>=100) {
                    MONITORINFO mi{};mi.cbSize=sizeof(mi);
                    GetMonitorInfoW(MonitorFromWindow(fixture,MONITOR_DEFAULTTONEAREST),&mi);
                    destination={int(mi.rcMonitor.left)+50,int(mi.rcMonitor.top)+50,
                                 std::min(1880,int(mi.rcMonitor.right-mi.rcMonitor.left)-100),
                                 std::min(1600,int(mi.rcMonitor.bottom-mi.rcMonitor.top)-100)};
                    source={x+100,y+100,sw,sh};
                }
                lens::Color color{170,23,91,static_cast<unsigned char>((i%3)*127+(i%3==2))};
                if (renderer.frameReady()) WaitForSingleObject(renderer.frameReady(),1000);
                if (!renderer.render(overlay,destination,source,color)) {
                    std::cerr << "HRESULT=0x" << std::hex << renderer.error() << '\n';
                    require(false,"Capture/present failed");
                }
                require(renderer.cursorDisabled(),"Cursor capture not explicitly disabled");
                ShowWindow(overlay,SW_SHOWNOACTIVATE);
                DwmFlush();
                // Recapture with the magnified window overlapping the source:
                // every captured pixel must still match the underlying fixture.
                if (renderer.frameReady()) WaitForSingleObject(renderer.frameReady(),1000);
                require(renderer.render(overlay,destination,source,color),"Recapture failed");
                std::vector<std::uint32_t> captured;
                require(renderer.readSource(source,captured),"GPU source readback failed");
                if(i==60 || i==90) {
                    // WGC delivers desktop content asynchronously. Wait a
                    // bounded number of presentation slots for the deliberately
                    // changed fixture, then validate EVERY pixel as usual.
                    auto complete=[&]() {
                        for(int row=0;row<sh;++row) for(int col=0;col<sw;++col) {
                            auto expected=tile(source.x-x+col,source.y-y+row);
                            auto rgb=(std::uint32_t(GetRValue(expected))<<16)|
                                     (std::uint32_t(GetGValue(expected))<<8)|GetBValue(expected);
                            if((captured[row*sw+col]&0xFFFFFFu)!=rgb) return false;
                        }
                        return true;
                    };
                    for(int retry=0;retry<8 && !complete();++retry) {
                        WaitForSingleObject(renderer.frameReady(),1000);
                        require(renderer.render(overlay,destination,source,color),"Updated desktop frame failed");
                        require(renderer.readSource(source,captured),"Updated desktop readback failed");
                    }
                }
                for(int row=0;row<sh;++row) for(int col=0;col<sw;++col) {
                    const COLORREF expected=tile(source.x-x+col,source.y-y+row);
                    const auto rgb=(std::uint32_t(GetRValue(expected))<<16)|
                                   (std::uint32_t(GetGValue(expected))<<8)|GetBValue(expected);
                    if ((captured[row*sw+col]&0xFFFFFFu)!=rgb) {
                        std::cerr << "step=" << i << " pixel=" << col << ',' << row << " expected=0x" << std::hex << rgb
                                  << " got=0x" << (captured[row*sw+col]&0xFFFFFFu) << std::dec << '\n';
                        require(false,"Self-capture or stale/incorrect source pixels detected");
                    }
                }
                std::vector<std::uint32_t> composed;
                require(renderer.readComposed(destination,composed),"GPU output readback failed");
                require(composed[0]==lens::premultiplied(color),"Border alpha/color mismatch");
                require((composed[4*destination.w+4]>>24)==255,"Lens interior not opaque");
                // Outside the rectangular lens the full-monitor composition
                // surface must remain transparent.
                std::vector<std::uint32_t> outside;
                auto bounds=renderer.monitorBounds();
                require(renderer.readComposed({int(bounds.left),int(bounds.top),1,1},outside) &&
                        outside[0]==0,"Outside of lens not transparent");
                RECT actual{}; GetWindowRect(overlay,&actual);
                require(EqualRect(&actual,&bounds),"Composition window moved during a frame");
            }
            CURSORINFO cursor{};cursor.cbSize=sizeof(cursor);
            require(GetCursorInfo(&cursor) && (cursor.flags&CURSOR_SHOWING),"Visible cursor required for cursor regression test");
            require(cursor.ptScreenPos.x>=x && cursor.ptScreenPos.x<x+720 &&
                    cursor.ptScreenPos.y>=y && cursor.ptScreenPos.y<y+440,
                    "Cursor left the test fixture during the test");
            lens::Rect cursorArea{std::clamp(int(cursor.ptScreenPos.x)-64,x,x+720-128),
                                  std::clamp(int(cursor.ptScreenPos.y)-64,y,y+440-128),128,128};
            lens::Rect destination{x+20,y+20,600,340};
            auto matchesBackground=[&]() {
                std::vector<std::uint32_t> pixels;
                require(renderer.readSource(cursorArea,pixels),"Cursor area readback failed");
                for(int row=0;row<128;++row) for(int col=0;col<128;++col) {
                    auto expected=tile(cursorArea.x-x+col,cursorArea.y-y+row);
                    auto rgb=(std::uint32_t(GetRValue(expected))<<16)|
                             (std::uint32_t(GetGValue(expected))<<8)|GetBValue(expected);
                    if((pixels[row*128+col]&0xFFFFFFu)!=rgb) return false;
                }
                return true;
            };
            auto nextFrame=[&]() {
                if(renderer.frameReady()) WaitForSingleObject(renderer.frameReady(),1000);
                require(renderer.render(overlay,destination,cursorArea,{0,0,0,185}),"Cursor test frame failed");
            };
            // Positive control: prove that the pixel comparison can detect the
            // captured pointer instead of passing with a cursor outside the ROI.
            require(SUCCEEDED(renderer.setCursorForTest(true)),"Could not enable positive control");
            bool pointerDetected=false;
            for(int attempt=0;attempt<30;++attempt) {
                nextFrame();
                if(!matchesBackground()) {pointerDetected=true;break;}
            }
            require(pointerDetected,"Positive control did not detect captured cursor");
            require(SUCCEEDED(renderer.setCursorForTest(false)) && renderer.cursorDisabled(),
                    "Could not disable cursor capture");
            bool clean=false;
            for(int attempt=0;attempt<30;++attempt) {
                nextFrame();
                if(matchesBackground()) {clean=true;break;}
            }
            require(clean,"Cursor pixels remain after disabling capture");
            for(int i=0;i<8;++i) {nextFrame();require(matchesBackground(),"Cursor pixels reappeared");}
            std::cout << "PASS: cursor-positive control detected, cursor disabled, every pixel under visible pointer matches background\n";
        }
        require(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=initialObjects+1,"GDI objects leaked");
        std::cout << "PASS: 120 moving/resizing frames including 1880x1600, live desktop changes, "
                     "every source pixel, border alpha 0/127/255, opaque interior, transparent exterior, "
                     "stable composition window and GDI cleanup\n";
    } catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; result=1; }
    if(overlay) DestroyWindow(overlay);
    if(fixture) DestroyWindow(fixture);
    return result;
}
