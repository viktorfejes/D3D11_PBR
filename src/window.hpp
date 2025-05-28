#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <string>

struct Window {
    HWND hwnd;
    std::wstring title;
    uint32_t x;
    uint32_t y;
    uint16_t width;
    uint16_t height;
    bool should_close;
};

namespace window {

bool create(const wchar_t *title, uint16_t width, uint16_t height, Window *out_window);
void destroy(Window *window);
bool is_running(Window *window);
void proc_messages();
bool should_close(Window *window);

}; // namespace window
