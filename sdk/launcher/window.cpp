// The launcher's progress window: a status line, a progress bar and Cancel. Updates come from the build thread as
// posted messages, so only the window's own thread touches its controls.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>

#include <algorithm>

#include "launcher.h"

namespace launcher {

namespace {

constexpr UINT kMsgStatus = WM_APP + 1;    // lParam: new std::wstring, deleted here
constexpr UINT kMsgFraction = WM_APP + 2;  // wParam: 0..10000, or -1 for marquee
constexpr UINT kMsgFinish = WM_APP + 3;
constexpr int kIdCancel = 100;
constexpr int kWidth = 520, kHeight = 170;

struct Controls {
    HWND status = nullptr, bar = nullptr, cancel = nullptr;
    Progress* owner = nullptr;
    bool marquee = false;
    HFONT font = nullptr;
};

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = reinterpret_cast<Controls*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            c = static_cast<Controls*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(c));
            NONCLIENTMETRICSW m{sizeof(m)};
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(m), &m, 0);
            c->font = CreateFontIndirectW(&m.lfMessageFont);
            HINSTANCE inst = cs->hInstance;
            c->status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 16, 16,
                                        kWidth - 48, 40, hwnd, nullptr, inst, nullptr);
            c->bar = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, 16, 62, kWidth - 48, 20, hwnd, nullptr,
                                     inst, nullptr);
            SendMessageW(c->bar, PBM_SETRANGE32, 0, 10000);
            c->cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, kWidth - 128, 94,
                                        96, 28, hwnd, reinterpret_cast<HMENU>(INT_PTR(kIdCancel)), inst, nullptr);
            for (HWND h : {c->status, c->bar, c->cancel}) SendMessageW(h, WM_SETFONT, WPARAM(c->font), TRUE);
            return 0;
        }
        case kMsgStatus: {
            auto* text = reinterpret_cast<std::wstring*>(lp);
            if (c) SetWindowTextW(c->status, text->c_str());
            delete text;
            return 0;
        }
        case kMsgFraction: {
            if (!c) return 0;
            bool marquee = int(wp) < 0;
            if (marquee != c->marquee) {
                LONG_PTR style = GetWindowLongPtrW(c->bar, GWL_STYLE);
                SetWindowLongPtrW(c->bar, GWL_STYLE, marquee ? style | PBS_MARQUEE : style & ~LONG_PTR(PBS_MARQUEE));
                SendMessageW(c->bar, PBM_SETMARQUEE, marquee, 30);
                c->marquee = marquee;
            }
            if (!marquee) SendMessageW(c->bar, PBM_SETPOS, wp, 0);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == kIdCancel && c && c->owner) {
                c->owner->Cancel();
                EnableWindow(c->cancel, FALSE);
                SetWindowTextW(c->status, L"Cancelling...");
            }
            return 0;
        case WM_CLOSE:
            if (c && c->owner && !c->owner->Cancelled()) {
                c->owner->Cancel();
                EnableWindow(c->cancel, FALSE);
                SetWindowTextW(c->status, L"Cancelling...");
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        case kMsgFinish:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (c && c->font) DeleteObject(c->font);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

Controls g_controls;

}  // namespace

bool Progress::Open(const std::wstring& title) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"XbeRecompLauncher";
    RegisterClassW(&wc);
    g_controls = {};
    g_controls.owner = this;
    RECT r{0, 0, kWidth, kHeight};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, title.c_str(), style, x, y, w, h, nullptr, nullptr, inst, &g_controls);
    if (!hwnd) return false;
    hwnd_ = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return true;
}

void Progress::Status(const std::wstring& text) {
    if (hwnd_) PostMessageW(static_cast<HWND>(hwnd_), kMsgStatus, 0, reinterpret_cast<LPARAM>(new std::wstring(text)));
}

void Progress::Fraction(double f) {
    WPARAM v = f < 0 ? WPARAM(-1) : WPARAM(std::min(1.0, f) * 10000);
    if (hwnd_) PostMessageW(static_cast<HWND>(hwnd_), kMsgFraction, v, 0);
}

void Progress::Finish() {
    if (hwnd_) PostMessageW(static_cast<HWND>(hwnd_), kMsgFinish, 0, 0);
}

void Progress::Run() {
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    hwnd_ = nullptr;
}

}  // namespace launcher
