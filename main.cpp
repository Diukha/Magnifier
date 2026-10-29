#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include "geometry.h"
#include "color.h"
#include "renderer.h"

namespace {
constexpr wchar_t kClass[] = L"MouseLens.Window.v1";
constexpr UINT kUpdate = WM_APP + 1;
HWND host = nullptr;
lens::Renderer renderer;
lens::Color borderColor = lens::kDefaultBorder;
HHOOK keyboardHook = nullptr, mouseHook = nullptr;
lens::State state;
bool closing = false;
bool heldAtLaunch[256] = {};
std::wstring settingsPath;

void CloseLens() {
    if (closing) return;
    closing = true;
    ShowWindow(host, SW_HIDE);
    PostMessageW(host, WM_CLOSE, 0, 0);
}

void Update() {
    if (closing || !host) return;
    POINT p{};
    if (!GetCursorPos(&p)) return;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &mi)) return;
    const RECT r = mi.rcMonitor;
    lens::Rect monitor{r.left, r.top, r.right-r.left, r.bottom-r.top};
    const auto frame = lens::window(state, monitor, p.x, p.y);
    lens::Rect interior{frame.x+lens::kBorder, frame.y+lens::kBorder,
                        frame.w-2*lens::kBorder, frame.h-2*lens::kBorder};
    const auto src = lens::source(interior, monitor, p.x, p.y, state.zoom);
    // Failed capture keeps the last complete frame; no blank frame is shown.
    if (!renderer.render(host, frame, src, borderColor))
        OutputDebugStringW(L"MouseLens: frame capture/presentation failed.\n");
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
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: closing = true; DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
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
    LoadSettings();
    WNDCLASSW cls{};
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = instance;
    cls.lpszClassName = kClass;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&cls);
    host = CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,
                          kClass, L"MouseLens", WS_POPUP, 0, 0, state.width, state.height,
                          nullptr, nullptr, instance, nullptr);
    if (!host || !SetWindowDisplayAffinity(host, WDA_EXCLUDEFROMCAPTURE)) {
        MessageBoxW(nullptr, L"Could not create a capture-excluded lens window. Windows 10 version 2004 or newer is required.", L"MouseLens", MB_OK|MB_ICONERROR);
        if (host) DestroyWindow(host);
        CloseHandle(mutex); return 1;
    }
    for (int i=0;i<256;++i) heldAtLaunch[i] = (GetAsyncKeyState(i)&0x8000)!=0;
    keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, Keyboard, instance, 0);
    mouseHook = SetWindowsHookExW(WH_MOUSE_LL, Mouse, instance, 0);
    if (!keyboardHook || !mouseHook) {
        if (keyboardHook) UnhookWindowsHookEx(keyboardHook);
        if (mouseHook) UnhookWindowsHookEx(mouseHook);
        DestroyWindow(host);
        CloseHandle(mutex);
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
    CloseHandle(mutex);
    return result == -1 ? 1 : 0;
}
