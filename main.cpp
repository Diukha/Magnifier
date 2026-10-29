#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <magnification.h>
#include <string>
#include "geometry.h"
#include "color.h"

// MinGW's magnification.h supplies the structures but omits these desktop APIs.
extern "C" {
__declspec(dllimport) BOOL WINAPI MagInitialize();
__declspec(dllimport) BOOL WINAPI MagUninitialize();
__declspec(dllimport) BOOL WINAPI MagSetWindowSource(HWND, RECT);
__declspec(dllimport) BOOL WINAPI MagSetWindowTransform(HWND, PMAGTRANSFORM);
__declspec(dllimport) BOOL WINAPI MagSetWindowFilterList(HWND, DWORD, int, HWND*);
}
constexpr wchar_t kMagnifierClass[] = L"Magnifier";

namespace {
constexpr wchar_t kClass[] = L"MouseLens.Window.v1";
constexpr wchar_t kFrameClass[] = L"MouseLens.Frame.v1";
constexpr UINT kUpdate = WM_APP + 1;
constexpr int kBorder = 3;
HWND host = nullptr, magnifier = nullptr;
HWND borderWindows[4] = {};
lens::Color borderColor = lens::kDefaultBorder;
HHOOK keyboardHook = nullptr, mouseHook = nullptr;
lens::State state;
bool closing = false;
bool heldAtLaunch[256] = {};
std::wstring settingsPath;

void CloseLens() {
    if (closing) return;
    closing = true;
    for (HWND border : borderWindows) if (border) ShowWindow(border, SW_HIDE);
    ShowWindow(host, SW_HIDE);
    PostMessageW(host, WM_CLOSE, 0, 0);
}

void Update() {
    if (closing || !magnifier) return;
    POINT p{};
    if (!GetCursorPos(&p)) return;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &mi)) return;
    const RECT r = mi.rcMonitor;
    lens::Rect monitor{r.left, r.top, r.right-r.left, r.bottom-r.top};
    const auto frame = lens::window(state, monitor, p.x, p.y);
    SetWindowPos(host, HWND_TOPMOST, frame.x+kBorder, frame.y+kBorder,
                 frame.w-2*kBorder, frame.h-2*kBorder,
                 SWP_NOACTIVATE | SWP_NOREDRAW);
    MoveWindow(magnifier, 0, 0, frame.w-2*kBorder, frame.h-2*kBorder, FALSE);
    // Separate, non-overlapping strips let only the border be translucent.
    // The magnified contents retain full opacity, including black pixels.
    const lens::Rect strips[] = {
        {frame.x, frame.y, frame.w, kBorder},
        {frame.x, frame.y+frame.h-kBorder, frame.w, kBorder},
        {frame.x, frame.y+kBorder, kBorder, frame.h-2*kBorder},
        {frame.x+frame.w-kBorder, frame.y+kBorder, kBorder, frame.h-2*kBorder}
    };
    for (int i=0; i<4; ++i) {
        const auto& strip = strips[i];
        SetWindowPos(borderWindows[i], HWND_TOPMOST, strip.x, strip.y, strip.w, strip.h,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    lens::Rect interior{frame.x+kBorder, frame.y+kBorder,
                        frame.w-2*kBorder, frame.h-2*kBorder};
    const auto src = lens::source(interior, monitor, p.x, p.y, state.zoom);
    MAGTRANSFORM matrix{};
    matrix.v[0][0] = state.zoom;
    matrix.v[1][1] = state.zoom;
    matrix.v[2][2] = 1.0f;
    MagSetWindowTransform(magnifier, &matrix);
    MagSetWindowSource(magnifier, {src.x, src.y, src.x+src.w, src.y+src.h});
    InvalidateRect(host, nullptr, FALSE);
    InvalidateRect(magnifier, nullptr, TRUE);
}

LRESULT CALLBACK Keyboard(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION && !closing) {
        const auto* key = reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
        const DWORD vk = key->vkCode;
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        if (vk < 256 && heldAtLaunch[vk]) {
            if (!down) heldAtLaunch[vk] = false;
            return CallNextHookEx(keyboardHook, code, message, data);
        }
        if (vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT) {
            if (down) {
                state.resize(vk == VK_RIGHT ? 40 : vk == VK_LEFT ? -40 : 0,
                             vk == VK_UP ? 40 : vk == VK_DOWN ? -40 : 0);
                PostMessageW(host, kUpdate, 0, 0);
            }
            return 1;
        }
        if (down) CloseLens();
    }
    return CallNextHookEx(keyboardHook, code, message, data);
}

LRESULT CALLBACK Mouse(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION && !closing) {
        if (message == WM_MOUSEWHEEL) {
            const auto* mouse = reinterpret_cast<MSLLHOOKSTRUCT*>(data);
            state.wheel(static_cast<short>(HIWORD(mouse->mouseData)));
            PostMessageW(host, kUpdate, 0, 0);
            return 1;
        }
        if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
            message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN) CloseLens();
    }
    return CallNextHookEx(mouseHook, code, message, data);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER:
    case kUpdate: Update(); return 0;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_CLOSE: closing = true; DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r{}; GetClientRect(hwnd, &r);
        HBRUSH brush = CreateSolidBrush(RGB(borderColor.r, borderColor.g, borderColor.b));
        FillRect(dc, &r, brush);
        DeleteObject(brush);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void LoadSettings() {
    wchar_t path[32768]{};
    DWORD n = GetModuleFileNameW(nullptr, path, 32768);
    if (!n || n >= 32768) return;
    settingsPath = path;
    settingsPath = settingsPath.substr(0, settingsPath.find_last_of(L"\\/")+1) + L"MouseLens.ini";
    state.width = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Lens", L"Width", 640, settingsPath.c_str())),160,2400);
    state.height = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Lens", L"Height", 320, settingsPath.c_str())),100,1600);
    state.zoom = std::clamp(GetPrivateProfileIntW(L"Lens", L"ZoomPercent", 200, settingsPath.c_str())/100.0f,1.0f,12.0f);
    wchar_t color[128]{};
    GetPrivateProfileStringW(L"Lens", L"BorderColor", L"", color, 128, settingsPath.c_str());
    if (!color[0]) {
        borderColor = lens::kDefaultBorder;
        WritePrivateProfileStringW(L"Lens", L"BorderColor", L"#377DDCFF", settingsPath.c_str());
    } else borderColor = lens::parseColor(color);
}
void SaveSettings() {
    if (settingsPath.empty()) return;
    WritePrivateProfileStringW(L"Lens", L"Width", std::to_wstring(state.width).c_str(), settingsPath.c_str());
    WritePrivateProfileStringW(L"Lens", L"Height", std::to_wstring(state.height).c_str(), settingsPath.c_str());
    WritePrivateProfileStringW(L"Lens", L"ZoomPercent", std::to_wstring(static_cast<int>(state.zoom*100+0.5f)).c_str(), settingsPath.c_str());
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\MouseLens.SingleInstance.v1");
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kClass, nullptr)) PostMessageW(existing, WM_CLOSE, 0, 0);
        CloseHandle(mutex);
        return 0;
    }
    if (!MagInitialize()) {
        MessageBoxW(nullptr, L"Windows Magnification API could not initialize.", L"MouseLens", MB_OK|MB_ICONERROR);
        CloseHandle(mutex); return 1;
    }
    LoadSettings();
    WNDCLASSW cls{};
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = instance;
    cls.lpszClassName = kClass;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&cls);
    WNDCLASSW frameCls = cls;
    frameCls.lpfnWndProc = FrameProc;
    frameCls.lpszClassName = kFrameClass;
    RegisterClassW(&frameCls);
    host = CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,
                          kClass, L"MouseLens", WS_POPUP, 0, 0, state.width, state.height,
                          nullptr, nullptr, instance, nullptr);
    if (host) {
        SetLayeredWindowAttributes(host, 0, 255, LWA_ALPHA);
        // Do not use MS_SHOWMAGNIFIEDCURSOR: Windows already draws the real
        // pointer above this window. Capturing it creates a second, lagging copy.
        magnifier = CreateWindowExW(0, kMagnifierClass, L"Lens", WS_CHILD|WS_VISIBLE,
                                   0,0,state.width-2*kBorder,state.height-2*kBorder,
                                   host,nullptr,instance,nullptr);
    }
    bool bordersReady = host != nullptr;
    if (host) for (HWND& border : borderWindows) {
        border = CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,
                                 kFrameClass, L"", WS_POPUP, 0, 0, 1, 1, host, nullptr, instance, nullptr);
        if (!border || !SetLayeredWindowAttributes(border, 0, borderColor.a, LWA_ALPHA)) bordersReady = false;
    }
    if (!host || !magnifier || !bordersReady) {
        MessageBoxW(nullptr, L"Could not create the magnifier window.", L"MouseLens", MB_OK|MB_ICONERROR);
        if (host) DestroyWindow(host);
        MagUninitialize(); CloseHandle(mutex); return 1;
    }
    HWND excluded[] = {host, borderWindows[0], borderWindows[1], borderWindows[2], borderWindows[3]};
    MagSetWindowFilterList(magnifier, MW_FILTERMODE_EXCLUDE, 5, excluded);
    for (int i=0;i<256;++i) heldAtLaunch[i] = (GetAsyncKeyState(i)&0x8000)!=0;
    keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, Keyboard, instance, 0);
    mouseHook = SetWindowsHookExW(WH_MOUSE_LL, Mouse, instance, 0);
    if (!keyboardHook || !mouseHook) {
        if (keyboardHook) UnhookWindowsHookEx(keyboardHook);
        if (mouseHook) UnhookWindowsHookEx(mouseHook);
        DestroyWindow(host);
        MagUninitialize(); CloseHandle(mutex);
        MessageBoxW(nullptr, L"Could not install input hooks.", L"MouseLens", MB_OK|MB_ICONERROR);
        return 1;
    }
    Update();
    ShowWindow(host, SW_SHOWNOACTIVATE);
    SetTimer(host, 1, 16, nullptr);
    MSG msg{};
    int result;
    while ((result = GetMessageW(&msg,nullptr,0,0)) > 0) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    UnhookWindowsHookEx(keyboardHook);
    UnhookWindowsHookEx(mouseHook);
    SaveSettings();
    MagUninitialize();
    CloseHandle(mutex);
    return result == -1 ? 1 : 0;
}
