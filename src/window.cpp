#include "window.hpp"

#include "application.hpp"
#include "input.hpp"
#include "logger.hpp"
#include "renderer.hpp"

#include <cassert>
#include <windows.h>

#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))

#define DEFAULT_WIN_CLASS_NAME L"DefaultWinClassName"

static LRESULT CALLBACK winproc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param);
static KeyCode VKToKeyCode(WPARAM w_param);

bool window::create(const wchar_t *title, uint16_t width, uint16_t height, Window *out_window) {
    assert(out_window && "window::create: The provided out_window pointer cannot be NULL");

    // We'll register a default window class here,
    // as we'll only use a single window throughout.
    HINSTANCE h_instance = GetModuleHandle(NULL);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = winproc;
    wc.hInstance = h_instance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = DEFAULT_WIN_CLASS_NAME;

    if (!RegisterClassExW(&wc)) {
        LOG("window::create: RegisterClassExW failed.");
        return false;
    }

    // Fill out member variables
    out_window->width = width;
    out_window->height = height;
    out_window->title = title;

    // Adjust window size to account for window decorations
    RECT wr = {0, 0, (LONG)out_window->width, (LONG)out_window->height};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    out_window->hwnd = CreateWindowExW(
        0,
        DEFAULT_WIN_CLASS_NAME,
        title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left,
        wr.bottom - wr.top,
        NULL,
        NULL,
        h_instance,
        out_window);

    if (!out_window->hwnd) {
        LOG("Window creation failed. Error: CreateWindowExW");
        return false;
    }

    // Set the boolean flag to false so it's open
    out_window->should_close = false;

    // Show the window
    ShowWindow(out_window->hwnd, SW_SHOW);

    return true;
}

void window::destroy(Window *window) {
    assert(window && "window::destroy: Pointer to the window MUST NOT be NULL");

    if (window->hwnd) {
        DestroyWindow(window->hwnd);
    }
    window->hwnd = nullptr;
}

void window::proc_messages() {
    MSG msg = {};
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool window::should_close(Window *window) {
    return window->should_close;
}

static LRESULT CALLBACK winproc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param) {
    Window *window = reinterpret_cast<Window *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCT *create_struct = reinterpret_cast<CREATESTRUCT *>(l_param);
            Window *window = reinterpret_cast<Window *>(create_struct->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
            return 0;
        }

        case WM_SIZE: {
            window->width = LOWORD(l_param);
            window->height = HIWORD(l_param);

            // Notify the Renderer
            // NOTE: Not resizing if minimized or if width/height is 0.
            // Also, we are directly notifying it right now, but it could
            // be done through some event system.
            Renderer *renderer = application::get_renderer();
            if (renderer && w_param != SIZE_MINIMIZED && window->width > 0 && window->height > 0) {
                renderer::on_window_resize(renderer, window->width, window->height);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            int32_t x_pos = GET_X_LPARAM(l_param);
            int32_t y_pos = GET_Y_LPARAM(l_param);
            input::process_mouse_move(x_pos, y_pos);
        }

        case WM_KEYDOWN: {
            input::process_key(VKToKeyCode(w_param), true);
        } break;

        case WM_KEYUP: {
            input::process_key(VKToKeyCode(w_param), false);
        } break;

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP: {
            bool pressed = msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN;
            if (pressed)  SetCapture(hwnd); else ReleaseCapture();
            MouseButton mb = MOUSE_BUTTON_COUNT;
            switch (msg) {
                case WM_LBUTTONDOWN:
                case WM_LBUTTONUP:
                    mb = MOUSE_BUTTON_LEFT;
                    break;
                case WM_RBUTTONDOWN:
                case WM_RBUTTONUP:
                    mb = MOUSE_BUTTON_RIGHT;
                    break;
                case WM_MBUTTONDOWN:
                case WM_MBUTTONUP:
                    mb = MOUSE_BUTTON_MIDDLE;
                    break;
            }
            if (mb < MOUSE_BUTTON_COUNT) {
                input::process_mouse_button(mb, pressed);
            }
        } break;

        case WM_MOUSEWHEEL: {
            int32_t delta = GET_WHEEL_DELTA_WPARAM(w_param);
            if (delta != 0) {
                // Flatten the input to (-1,1)
                delta = delta < 0 ? -1 : 1;
            }
            input::process_mouse_wheel(delta);
        } break;

        case WM_CLOSE:
            // Init shutdown
            // TODO: revisit this, as I'm destroying the window here!
            // DestroyWindow(hwnd);
            window->should_close = true;
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, w_param, l_param);
}

static KeyCode VKToKeyCode(WPARAM w_param) {
    switch (w_param) {
        case '0':
            return KEY_0;
        case '1':
            return KEY_1;
        case '2':
            return KEY_2;
        case '3':
            return KEY_3;
        case '4':
            return KEY_4;
        case '5':
            return KEY_5;
        case '6':
            return KEY_6;
        case '7':
            return KEY_7;
        case '8':
            return KEY_8;
        case '9':
            return KEY_9;
        case 'Q':
            return KEY_Q;
        case 'W':
            return KEY_W;
        case 'E':
            return KEY_E;
        case 'R':
            return KEY_R;
        default:
            return KEY_UNKNOWN;
    }
}
