// Extra Tick - status window + hotkey (Windows only).
//
// WHAT THIS ADDS
//   1. A small always-on-top window, separate from the game window, that shows
//      "EXTRA TICK: ON (3 ticks)" on green or "EXTRA TICK: OFF" on red.
//      You can drag it anywhere. It never takes keyboard focus from GD.
//   2. A hotkey (default F7, changeable in the mod settings) that flips the
//      "enabled" setting. It only fires while Geometry Dash is the focused
//      window, so pressing the key in a browser etc. does nothing.
//
// HOW IT IS WIRED
//   - The click logic itself must live inside the game (see hooks.cpp), so this
//     file only DISPLAYS state and FLIPS the setting. It never touches physics.
//   - The hotkey is polled once per frame from a CCScheduler::update hook, so
//     it runs on the game's main thread and can safely read/write mod settings.
//   - The window lives on its own thread (Win32 needs a message loop). It never
//     touches Geode settings; it only reads three std::atomic values that the
//     main-thread hook keeps up to date.
//
// PLATFORMS
//   Windows only. On macOS/Android this file compiles to nothing, and the mod
//   still works - there is just no window and no hotkey.
//
// GD-UPDATE / GEODE-UPDATE
//   Most likely to need changes if Geode's modify headers or Notification API
//   change. Nothing here depends on GD's own classes except CCScheduler (cocos).

#include <Geode/Geode.hpp>

#ifdef GEODE_IS_WINDOWS

#include <Geode/modify/CCScheduler.hpp>
#include <Geode/ui/Notification.hpp>
#include <atomic>
#include <string>
#include <thread>
#include <cwchar>
#include <windows.h>

using namespace geode::prelude;

namespace {

    // ---- State shared between the game thread and the window thread ----
    std::atomic<bool> g_on{true};
    std::atomic<int>  g_window{2};
    std::atomic<bool> g_show{true};

    // ---- Hotkey name (mod setting) -> Windows virtual-key code ----
    // Keep this list in sync with the "one-of" list in mod.json.
    int keyFromName(std::string const& name) {
        if (name.size() >= 2 && name[0] == 'F') {
            int n = std::atoi(name.c_str() + 1);
            if (n >= 1 && n <= 12) return VK_F1 + (n - 1);
        }
        if (name == "Insert")   return VK_INSERT;
        if (name == "Home")     return VK_HOME;
        if (name == "End")      return VK_END;
        if (name == "PageUp")   return VK_PRIOR;
        if (name == "PageDown") return VK_NEXT;
        if (name == "Pause")    return VK_PAUSE;
        return VK_F7;
    }

    // True if the foreground window belongs to this process (i.e. GD).
    bool gameHasFocus() {
        HWND fg = GetForegroundWindow();
        if (!fg) return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        return pid == GetCurrentProcessId();
    }

    // ---- The status window ----

    LRESULT CALLBACK statusProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_TIMER:
                // Show/hide according to the setting, then repaint.
                ShowWindow(hwnd, g_show.load() ? SW_SHOWNOACTIVATE : SW_HIDE);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;

            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC dc = BeginPaint(hwnd, &ps);
                RECT rc;
                GetClientRect(hwnd, &rc);

                const bool on = g_on.load();
                HBRUSH bg = CreateSolidBrush(on ? RGB(28, 135, 62) : RGB(155, 38, 38));
                FillRect(dc, &rc, bg);
                DeleteObject(bg);

                wchar_t text[64];
                if (on) {
                    const int w = g_window.load();
                    swprintf(text, 64, L"EXTRA TICK: ON  (%d tick%ls)", w, w == 1 ? L"" : L"s");
                } else {
                    swprintf(text, 64, L"EXTRA TICK: OFF");
                }

                HFONT font = CreateFontW(
                    -18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                    DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                HGDIOBJ old = SelectObject(dc, font);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(255, 255, 255));
                DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(dc, old);
                DeleteObject(font);

                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_MOUSEACTIVATE:
                return MA_NOACTIVATE;       // clicking it must not steal focus from GD

            case WM_NCHITTEST:
                return HTCAPTION;           // drag the window by clicking anywhere on it

            case WM_ERASEBKGND:
                return 1;                   // we paint everything ourselves
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    void statusThread() {
        HINSTANCE inst = GetModuleHandleW(nullptr);

        WNDCLASSW wc{};
        wc.lpfnWndProc = statusProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"ExtraTickStatusWindow";
        RegisterClassW(&wc);

        // TOPMOST: stays above GD. TOOLWINDOW: no taskbar button.
        // NOACTIVATE: never becomes the active window.
        HWND hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            wc.lpszClassName, L"Extra Tick",
            WS_POPUP | WS_BORDER,
            20, 20, 260, 44,
            nullptr, nullptr, inst, nullptr);
        if (!hwnd) return;

        SetTimer(hwnd, 1, 100, nullptr);    // repaint / show-hide 10x per second
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

// Start the window thread once when the mod loads.
$execute {
    std::thread(statusThread).detach();
}

// HOOK: CCScheduler::update runs once per frame on the main thread, always
// (menus and levels alike). We use it as a safe place to poll the hotkey and
// copy settings into the atomics above.
class $modify(ExtraTickScheduler, CCScheduler) {
    void update(float dt) {
        CCScheduler::update(dt);

        auto mod = Mod::get();

        // Keep the window's view of the settings fresh.
        g_on.store(mod->getSettingValue<bool>("enabled"));
        g_window.store(static_cast<int>(mod->getSettingValue<int64_t>("tick-window")));
        g_show.store(mod->getSettingValue<bool>("status-window"));

        // Hotkey with edge detection: toggle once per key press, not every frame.
        static bool wasDown = false;
        const int vk = keyFromName(mod->getSettingValue<std::string>("toggle-key"));
        const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;

        if (down && !wasDown && gameHasFocus()) {
            const bool now = !mod->getSettingValue<bool>("enabled");
            mod->setSettingValue<bool>("enabled", now);
            g_on.store(now);
            log::info("[ExtraTick] toggled {}", now ? "ON" : "OFF");
            Notification::create(
                now ? "Extra Tick: ON" : "Extra Tick: OFF",
                now ? NotificationIcon::Success : NotificationIcon::Info,
                0.8f
            )->show();
        }
        wasDown = down;
    }
};

#endif // GEODE_IS_WINDOWS
