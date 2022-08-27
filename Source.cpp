#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"") 

#include "BorderlessWindow.hpp"

#include <stdexcept>
#include <system_error>

#include <Windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <string.h>
#include <tchar.h>
#pragma comment(lib, "dwmapi.lib")

#define ID_EXIT_BUTTON 101
#define ID_MINIMIZED_BUTTON 102

namespace {
    // we cannot just use WS_POPUP style
    // WS_THICKFRAME: without this the window cannot be resized and so aero snap, de-maximizing and minimizing won't work
    // WS_SYSMENU: enables the context menu with the move, close, maximize, minize... commands (shift + right-click on the task bar item)
    // WS_CAPTION: enables aero minimize animation/transition
    // WS_MAXIMIZEBOX, WS_MINIMIZEBOX: enable minimize/maximize
    enum class Style : DWORD {
        windowed = WS_POPUP | WS_CAPTION | WS_BORDER,
        aero_borderless = WS_POPUP | WS_CAPTION | WS_BORDER,
        basic_borderless = WS_POPUP | WS_CAPTION | WS_BORDER
    };

    auto maximized(HWND hwnd) -> bool {
        WINDOWPLACEMENT placement;
        if (!::GetWindowPlacement(hwnd, &placement)) {
            return false;
        }

        return placement.showCmd == false;
    }

    /* Adjust client rect to not spill over monitor edges when maximized.
     * rect(in/out): in: proposed window rect, out: calculated client rect
     * Does nothing if the window is not maximized.
     */
    auto adjust_maximized_client_rect(HWND window, RECT& rect) -> void {
        if (!maximized(window)) {
            return;
        }

        auto monitor = ::MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
        if (!monitor) {
            return;
        }

        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        if (!::GetMonitorInfoW(monitor, &monitor_info)) {
            return;
        }

        // when maximized, make the client area fill just the monitor (without task bar) rect,
        // not the whole window rect which extends beyond the monitor.
        rect = monitor_info.rcWork;
    }

    auto last_error(const std::string& message) -> std::system_error {
        return std::system_error(
            std::error_code(::GetLastError(), std::system_category()),
            message
        );
    }

    auto window_class(WNDPROC wndproc) -> const wchar_t* {
        static const wchar_t* window_class_name = [&] {
            WNDCLASSEXW wcx{};
            wcx.cbSize = sizeof(wcx);
            wcx.style = CS_HREDRAW | CS_VREDRAW;
            wcx.hInstance = nullptr;
            wcx.lpfnWndProc = wndproc;
            wcx.lpszClassName = L"TSAWCLASS";
            wcx.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wcx.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
            const ATOM result = ::RegisterClassExW(&wcx);
            if (!result) {
                throw last_error("failed to register window class");
            }
            return wcx.lpszClassName;
        }();
        return window_class_name;
    }

    auto composition_enabled() -> bool {
        BOOL composition_enabled = FALSE;
        bool success = ::DwmIsCompositionEnabled(&composition_enabled) == S_OK;
        return composition_enabled && success;
    }

    auto select_borderless_style() -> Style {
        return composition_enabled() ? Style::aero_borderless : Style::basic_borderless;
    }

    auto set_shadow(HWND handle, bool enabled) -> void {
        if (composition_enabled()) {
            static const MARGINS shadow_state[2]{ { 0,0,0,0 },{ 1,1,1,1 } };
            ::DwmExtendFrameIntoClientArea(handle, &shadow_state[enabled]);
        }
    }

    auto create_window(WNDPROC wndproc, void* userdata) -> unique_handle {
        auto handle = CreateWindowExW(
            0, window_class(wndproc), L"Here is the title of the Window, will only show on the taskbar.",
            static_cast<DWORD>(Style::aero_borderless), CW_USEDEFAULT, CW_USEDEFAULT,
            450, 500, nullptr, nullptr, nullptr, userdata
        );

        if (!handle) {
            throw last_error("failed to create window");
        }
        return unique_handle{ handle };
    }
}

BorderlessWindow::BorderlessWindow()
    : handle{ create_window(&BorderlessWindow::WndProc, this) }
{
    set_borderless(borderless);
    set_borderless_shadow(borderless_shadow);
    ::ShowWindow(handle.get(), SW_SHOW);
}

void BorderlessWindow::set_borderless(bool enabled) {
    Style new_style = (enabled) ? select_borderless_style() : Style::windowed;
    Style old_style = static_cast<Style>(::GetWindowLongPtrW(handle.get(), GWL_STYLE));

    if (new_style != old_style) {
        borderless = enabled;

        ::SetWindowLongPtrW(handle.get(), GWL_STYLE, static_cast<LONG>(new_style));

        // when switching between borderless and windowed, restore appropriate shadow state
        set_shadow(handle.get(), borderless_shadow && (new_style != Style::windowed));

        //set window to center
        RECT rc;
        GetWindowRect(handle.get(), &rc);
        int xPos = (GetSystemMetrics(SM_CXSCREEN) - rc.right) / 2;
        int yPos = (GetSystemMetrics(SM_CYSCREEN) - rc.bottom) / 2;
        //SetWindowLong(handle.get(), GWL_STYLE, WS_MAXIMIZEBOX);
        SetWindowPos(handle.get(), 0, xPos, yPos, 450, 500, SWP_NOZORDER);

        // redraw frame
        ::SetWindowPos(handle.get(), 0, xPos, yPos, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
        ::ShowWindow(handle.get(), SW_SHOW);
    }
}

void BorderlessWindow::set_borderless_shadow(bool enabled) {
    if (borderless) {
        borderless_shadow = enabled;
        set_shadow(handle.get(), enabled);
    }
}

HFONT CreateTitleBarButton(HWND hwnd) {
    static HFONT s_hFont = NULL;
    const TCHAR* fontName = _T("Calibri (Body)"); //you can set custom font name here.
    const long nFontSize = 18;

    HDC hdc = GetDC(hwnd);

    LOGFONT logFont = { 0 };
    logFont.lfHeight = -MulDiv(nFontSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    logFont.lfWeight = FW_REGULAR;
    _tcscpy_s(logFont.lfFaceName, fontName);

    s_hFont = CreateFontIndirect(&logFont);

    ReleaseDC(hwnd, hdc);
    HWND btn_minimized = CreateWindowEx(NULL, TEXT("button"), TEXT("\u2212"),
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        0, 0, 0, 0, hwnd, (HMENU)ID_MINIMIZED_BUTTON, NULL, NULL);

    HWND btn_exit = CreateWindowEx(NULL, TEXT("button"), TEXT("\u00D7"),
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        0, 0, 0, 0, hwnd, (HMENU)ID_EXIT_BUTTON, NULL, NULL);

    SendMessage(btn_exit, WM_SETFONT, (WPARAM)s_hFont, (LPARAM)MAKELONG(TRUE, 0));
    SendMessage(btn_minimized, WM_SETFONT, (WPARAM)s_hFont, (LPARAM)MAKELONG(TRUE, 0));
    return s_hFont;
}

auto CALLBACK BorderlessWindow::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) noexcept -> LRESULT {
    static HFONT FONT_TITLEBAR = NULL;
    if (msg == WM_NCCREATE) {
        auto userdata = reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams;
        // store window instance pointer in window user data
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(userdata));
    }

    if (auto window_ptr = reinterpret_cast<BorderlessWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        auto& window = *window_ptr;

        switch (msg) {
        case WM_NCCALCSIZE: {
            if (wparam == TRUE && window.borderless) {
                auto& params = *reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
                adjust_maximized_client_rect(hwnd, params.rgrc[0]);
                return 0;
            }
            break;
        }
        case WM_NCHITTEST: {
            // When we have no border or title bar, we need to perform our
            // own hit testing to allow resizing and moving.
            if (window.borderless) {
                return window.hit_test(POINT{
                    GET_X_LPARAM(lparam),
                    GET_Y_LPARAM(lparam)
                    });
            }
            break;
        }
        case WM_NCACTIVATE: {
            if (!composition_enabled()) {
                // Prevents window frame reappearing on window activation
                // in "basic" theme, where no aero shadow is present.
                return 1;
            }
            break;
        }
        case WM_PAINT: {

            PAINTSTRUCT ps;
            HDC hdc;
            TCHAR greeting[] = _T("Hello, Windows desktop!");
            hdc = BeginPaint(hwnd, &ps);
            TextOut(hdc, 5, 5, greeting, _tcslen(greeting));
            EndPaint(hwnd, &ps);

            break;
        }
        case WM_CREATE: {
            FONT_TITLEBAR = CreateTitleBarButton(hwnd);
            break;
        }
        case WM_NOTIFY: {
            static HBRUSH BRUSH_BUTTON_MINIMIZED_NORMAL = NULL;
            static HBRUSH BRUSH_BUTTON_MINIMIZED_HOVER = NULL;
            static HBRUSH BRUSH_BUTTON_MINIMIZED_CLICKED = NULL;

            static HBRUSH BRUSH_BUTTON_EXIT_NORMAL = NULL;
            static HBRUSH BRUSH_BUTTON_EXIT_HOVER = NULL;
            static HBRUSH BRUSH_BUTTON_EXIT_CLICKED = NULL;

            LPNMHDR some_item = (LPNMHDR)lparam;
            if (some_item->idFrom == ID_EXIT_BUTTON && some_item->code == NM_CUSTOMDRAW) {
                LPNMCUSTOMDRAW item = (LPNMCUSTOMDRAW)some_item;
                if (item->uItemState & CDIS_SELECTED) {
                    if (BRUSH_BUTTON_EXIT_CLICKED == NULL)
                        BRUSH_BUTTON_EXIT_CLICKED = CreateSolidBrush(RGB(227, 61, 61));

                    //Select our color when the button is selected
                    if (BRUSH_BUTTON_EXIT_CLICKED == NULL)
                        BRUSH_BUTTON_EXIT_CLICKED = CreateSolidBrush(RGB(227, 61, 61));
                    //Create pen for button border
                    HPEN pen = CreatePen(PS_INSIDEFRAME, 0, RGB(227, 61, 61));
                    //Select our brush into hDC
                    HGDIOBJ old_pen = SelectObject(item->hdc, pen);
                    HGDIOBJ old_brush = SelectObject(item->hdc, BRUSH_BUTTON_EXIT_CLICKED);

                    //If you want rounded button, then use this, otherwise use FillRect().
                    RoundRect(item->hdc, item->rc.left, item->rc.top, item->rc.right, item->rc.bottom, 0, 0);

                    //Clean up
                    SelectObject(item->hdc, old_pen);
                    SelectObject(item->hdc, old_brush);
                    DeleteObject(pen);

                    //Now, I don't want to do anything else myself (draw text) so I use this value for return:
                    return CDRF_DODEFAULT;
                }
                else {
                    if (item->uItemState & CDIS_HOT) //Our mouse is over the button
                    {
                        if (BRUSH_BUTTON_EXIT_HOVER == NULL)
                            BRUSH_BUTTON_EXIT_HOVER = CreateSolidBrush(RGB(227, 61, 61));

                        HPEN pen = CreatePen(PS_INSIDEFRAME, 0, RGB(227, 61, 61));
                        HGDIOBJ old_pen = SelectObject(item->hdc, pen);
                        HGDIOBJ old_brush = SelectObject(item->hdc, BRUSH_BUTTON_EXIT_HOVER);
                        RoundRect(item->hdc, item->rc.left, item->rc.top, item->rc.right, item->rc.bottom, 0, 0);
                        SelectObject(item->hdc, old_pen);
                        SelectObject(item->hdc, old_brush);
                        DeleteObject(pen);
                        return CDRF_DODEFAULT;
                    }

                    //Select our color when our button is doing nothing
                    if (BRUSH_BUTTON_EXIT_NORMAL == NULL)
                        BRUSH_BUTTON_EXIT_NORMAL = CreateSolidBrush(RGB(255, 255, 255));

                    HPEN pen = CreatePen(PS_INSIDEFRAME, 0, RGB(255, 255, 255));
                    HGDIOBJ old_pen = SelectObject(item->hdc, pen);
                    HGDIOBJ old_brush = SelectObject(item->hdc, BRUSH_BUTTON_EXIT_NORMAL);
                    RoundRect(item->hdc, item->rc.left, item->rc.top, item->rc.right, item->rc.bottom, 0, 0);
                    SelectObject(item->hdc, old_pen);
                    SelectObject(item->hdc, old_brush);
                    DeleteObject(pen);

                    return CDRF_DODEFAULT;
                }
            }
            else if (some_item->idFrom == ID_MINIMIZED_BUTTON && some_item->code == NM_CUSTOMDRAW) {
                LPNMCUSTOMDRAW item = (LPNMCUSTOMDRAW)some_item;
                if (item->uItemState & CDIS_SELECTED) {
                    if (BRUSH_BUTTON_MINIMIZED_CLICKED == NULL)
                        BRUSH_BUTTON_MINIMIZED_CLICKED = CreateSolidBrush(RGB(232, 232, 232));//CreateGradientBrush(RGB(180, 0, 0), RGB(255, 180, 0), item);

                    //Select our color when the button is selected
                    if (BRUSH_BUTTON_MINIMIZED_CLICKED == NULL)
                        BRUSH_BUTTON_MINIMIZED_CLICKED = CreateSolidBrush(RGB(232, 232, 232));
                    //Create pen for button border
                    HPEN pen = CreatePen(PS_INSIDEFRAME, 0, RGB(232, 232, 232));
                    //Select our brush into hDC
                    HGDIOBJ old_pen = SelectObject(item->hdc, pen);
                    HGDIOBJ old_brush = SelectObject(item->hdc, BRUSH_BUTTON_MINIMIZED_CLICKED);

                    //If you want rounded button, then use this, otherwise use FillRect().
                    RoundRect(item->hdc, item->rc.left, item->rc.top, item->rc.right, item->rc.bottom, 0, 0);

                    //Clean up
                    SelectObject(item->hdc, old_pen);
                    SelectObject(item->hdc, old_brush);
                    DeleteObject(pen);

                    //Now, I don't want to do anything else myself (draw text) so I use this value for return:
                    return CDRF_DODEFAULT;
                }
                else {
                    if (item->uItemState & CDIS_HOT) //Our mouse is over the button
                    {
                        if (BRUSH_BUTTON_MINIMIZED_HOVER == NULL)
                            BRUSH_BUTTON_MINIMIZED_HOVER = CreateSolidBrush(RGB(214, 214, 214));

                        HPEN pen = CreatePen(PS_INSIDEFRAME, 0, RGB(214, 214, 214));
                        HGDIOBJ old_pen = SelectObject(item->hdc, pen);
                        HGDIOBJ old_brush = SelectObject(item->hdc, BRUSH_BUTTON_MINIMIZED_HOVER);
                        RoundRect(item->hdc, item->rc.left, item->rc.top, item->rc.right, item->rc.bottom, 0, 0);
                        SelectObject(item->hdc, old_pen);
                        SelectObject(item->hdc, old_brush);
                        DeleteObject(pen);
                        return CDRF_DODEFAULT;
                    }

                    //Select our color when our button is doing nothing
                    if (BRUSH_BUTTON_MINIMIZED_NORMAL == NULL)
                        BRUSH_BUTTON_MINIMIZED_NORMAL = CreateSolidBrush(RGB(255, 255, 255));

                    HPEN pen = CreatePen(PS_INSIDEFRAME, 0, RGB(255, 255, 255));
                    HGDIOBJ old_pen = SelectObject(item->hdc, pen);
                    HGDIOBJ old_brush = SelectObject(item->hdc, BRUSH_BUTTON_MINIMIZED_NORMAL);
                    RoundRect(item->hdc, item->rc.left, item->rc.top, item->rc.right, item->rc.bottom, 0, 0);
                    SelectObject(item->hdc, old_pen);
                    SelectObject(item->hdc, old_brush);
                    DeleteObject(pen);

                    return CDRF_DODEFAULT;
                }
            }
            break;
        }
        case WM_SIZE: {

            RECT rect;
            GetClientRect(hwnd, &rect);

            HWND btn_minimized = GetDlgItem(hwnd, ID_MINIMIZED_BUTTON);
            HWND btn_exit = GetDlgItem(hwnd, ID_EXIT_BUTTON);
            
            MoveWindow(btn_minimized, rect.right - 35 * 2, -1, 35, 28, FALSE);
            MoveWindow(btn_exit, rect.right - 35, -1, 35, 28, FALSE);

            break;
        }
        case WM_COMMAND: {
            if (LOWORD(wparam) == ID_MINIMIZED_BUTTON) {
                ShowWindow(hwnd, SW_MINIMIZE);
            }
            else if (LOWORD(wparam) == ID_EXIT_BUTTON) {
                SendMessage(hwnd, WM_CLOSE, 0, 0);
            }
            break;
        }
        case WM_CLOSE: {
            ::DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY: {
            DeleteObject(FONT_TITLEBAR);
            PostQuitMessage(0);
            return 0;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            switch (wparam) {
            case VK_F8: { window.borderless_drag = !window.borderless_drag;        return 0; }
            case VK_F9: { window.borderless_resize = !window.borderless_resize;    return 0; }
            case VK_F10: { window.set_borderless(!window.borderless);               return 0; }
            case VK_F11: { window.set_borderless_shadow(!window.borderless_shadow); return 0; }
            }
            break;
        }
        }
    }

    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

auto BorderlessWindow::hit_test(POINT cursor) const -> LRESULT {
    // identify borders and corners to allow resizing the window.
    // Note: On Windows 10, windows behave differently and
    // allow resizing outside the visible window frame.
    // This implementation does not replicate that behavior.
    const POINT border{
        ::GetSystemMetrics(SM_CXFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER),
        ::GetSystemMetrics(SM_CYFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER)
    };
    RECT window;
    if (!::GetWindowRect(handle.get(), &window)) {
        return HTNOWHERE;
    }

    const auto drag = borderless_drag ? HTCAPTION : HTCLIENT;

    enum region_mask {
        client = 0b0000,
        left = 0b0001,
        right = 0b0010,
        top = 0b0100,
        bottom = 0b1000,
    };

    const auto result =
        left * (cursor.x < (window.left + border.x)) |
        right * (cursor.x >= (window.right - border.x)) |
        top * (cursor.y < (window.top + border.y)) |
        bottom * (cursor.y >= (window.bottom - border.y));

    switch (result) {
    case left: return borderless_resize ? HTLEFT : drag;
    case right: return borderless_resize ? HTRIGHT : drag;
    case top: return borderless_resize ? HTTOP : drag;
    case bottom: return borderless_resize ? HTBOTTOM : drag;
    case top | left: return borderless_resize ? HTTOPLEFT : drag;
    case top | right: return borderless_resize ? HTTOPRIGHT : drag;
    case bottom | left: return borderless_resize ? HTBOTTOMLEFT : drag;
    case bottom | right: return borderless_resize ? HTBOTTOMRIGHT : drag;
    case client: return drag;
    default: return HTNOWHERE;
    }
}

int CALLBACK WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    try {
        BorderlessWindow window;
        MSG msg;
        while (::GetMessageW(&msg, nullptr, 0, 0) == TRUE) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }
    catch (const std::exception& e) {
        ::MessageBoxA(nullptr, e.what(), "Unhandled Exception", MB_OK | MB_ICONERROR);
    }
}