#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#include <windows.h>
LRESULT CALLBACK Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(w,&ps);
        RECT r{}; GetClientRect(w,&r);
        FillRect(dc,&r,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));
        SetBkMode(dc,TRANSPARENT);
        HFONT font=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        HGDIOBJ old=SelectObject(dc,font);
        const wchar_t* lines[]={L"MouseLens visual test",L"Small text: abcdefghijklmnopqrstuvwxyz 0123456789",
            L"Move the pointer here and use the wheel.",L"Arrow keys resize the lens. Escape closes it."};
        for(int i=0;i<4;++i) TextOutW(dc,30,40+i*60,lines[i],lstrlenW(lines[i]));
        wchar_t tick[80]{}; wsprintfW(tick,L"Live frame: %lu",GetTickCount()/1000);
        TextOutW(dc,30,300,tick,lstrlenW(tick));
        SelectObject(dc,old); DeleteObject(font); EndPaint(w,&ps); return 0;
    }
    if(msg==WM_TIMER){InvalidateRect(w,nullptr,FALSE);return 0;}
    if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(w,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,PWSTR,int){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW c{};c.lpfnWndProc=Proc;c.hInstance=h;c.lpszClassName=L"MouseLensTestFixture";
    c.hCursor=LoadCursorW(nullptr,IDC_ARROW); RegisterClassW(&c);
    HWND w=CreateWindowW(c.lpszClassName,L"MouseLens test fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        180,120,950,650,nullptr,nullptr,h,nullptr);
    SetTimer(w,1,1000,nullptr);MSG m{};
    while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}return 0;
}
