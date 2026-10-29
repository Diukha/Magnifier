#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#define LENS_PROFILE
#include <windows.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include "../renderer.h"

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const int width=GetSystemMetrics(SM_CXSCREEN),height=GetSystemMetrics(SM_CYSCREEN);
    std::cout << "Display: " << width << 'x' << height << '\n';
    HWND overlay=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOPMOST|WS_EX_TOOLWINDOW,
        L"STATIC",L"MouseLens benchmark",WS_POPUP,0,0,640,320,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!overlay || !SetWindowDisplayAffinity(overlay,WDA_EXCLUDEFROMCAPTURE)) return 1;
    lens::Renderer renderer;
    for (auto requested : {lens::Rect{0,0,640,320},lens::Rect{0,0,1880,1600},lens::Rect{0,0,2400,1600}}) {
        lens::State state;state.width=requested.w;state.height=requested.h;state.zoom=1.44f;
        std::vector<double> samples;
        double capture=0,scale=0,alpha=0,present=0;
        double elapsed=0;
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        lens::Rect destination{};
        for(int i=0;i<35;++i) {
            LARGE_INTEGER began{},finished{};QueryPerformanceCounter(&began);
            HANDLE ready=renderer.frameReady();
            if(ready) WaitForSingleObject(ready,1000);
            const int px=width/2+(i%20)-10,py=height/2+(i%10)-5;
            destination=lens::window(state,{0,0,width,height},px,py);
            auto interior=destination;interior.w-=6;interior.h-=6;
            auto source=lens::source(interior,{0,0,width,height},px,py,state.zoom);
            if(!renderer.render(overlay,destination,source,{0,0,0,185})) {
                std::cerr << "Render failed HRESULT=0x" << std::hex << renderer.error() << '\n';return 2;
            }
            ShowWindow(overlay,SW_SHOWNOACTIVATE);
            QueryPerformanceCounter(&finished);
            if(i>=5) {
                capture+=renderer.captureMs;scale+=renderer.scaleMs;
                alpha+=renderer.alphaMs;present+=renderer.presentMs;
                samples.push_back(renderer.captureMs+renderer.scaleMs+renderer.alphaMs+renderer.presentMs);
                elapsed+=(finished.QuadPart-began.QuadPart)*1000.0/frequency.QuadPart;
            }
        }
        std::sort(samples.begin(),samples.end());
        std::cout << std::fixed << std::setprecision(2) << destination.w << 'x' << destination.h
            << " capture=" << capture/30 << "ms scale=" << scale/30 << "ms alpha=" << alpha/30
            << "ms present=" << present/30 << "ms mean=" << (capture+scale+alpha+present)/30
            << "ms p95=" << samples[28] << "ms frame-with-pacing=" << elapsed/30
            << "ms fps=" << 30000.0/elapsed << '\n';
    }
    DestroyWindow(overlay);
}
