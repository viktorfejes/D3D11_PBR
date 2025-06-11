#pragma once

#include <cstdint>

enum KeyCode : uint8_t {
    KEY_UNKNOWN = 0,
    KEY_0,
    KEY_1,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_5,
    KEY_6,
    KEY_7,
    KEY_8,
    KEY_9,
    KEY_Q,
    KEY_W,
    KEY_E,
    KEY_R,
    KEY_COUNT
};

enum MouseButton : uint8_t {
    MOUSE_BUTTON_LEFT,
    MOUSE_BUTTON_RIGHT,
    MOUSE_BUTTON_MIDDLE,
    MOUSE_BUTTON_X1,
    MOUSE_BUTTON_X2,
    MOUSE_BUTTON_COUNT
};

struct KeyboardState {
    bool keys[KEY_COUNT];
};

struct MouseState {
    int16_t x, y;
    bool buttons[MOUSE_BUTTON_COUNT];
};

struct Input {
    KeyboardState keyboard_current;
    KeyboardState keyboard_previous;

    MouseState mouse_current;
    MouseState mouse_previous;
    int8_t mouse_scroll_delta;
};

namespace input {

bool initialize(Input *state);
void swap_buffers(Input *state);
void process_key(KeyCode key_code, bool pressed);
void process_mouse_button(MouseButton button, bool pressed);
void process_mouse_move(int16_t x, int16_t y);
void process_mouse_wheel(int32_t delta);

bool is_key_down(KeyCode key_code);
bool is_key_up(KeyCode key_code);
bool was_key_down(KeyCode key_code);
bool was_key_up(KeyCode key_code);
bool is_key_pressed(KeyCode key_code);
bool is_key_released(KeyCode key_code);

bool is_mouse_button_down(MouseButton button);
bool is_mouse_button_up(MouseButton button);
bool was_mouse_button_down(MouseButton button);
bool was_mouse_button_up(MouseButton button);
bool is_mouse_button_pressed(MouseButton button);
bool is_mouse_button_released(MouseButton button);

int16_t mouse_get_x();
int16_t mouse_get_y();
int16_t mouse_get_delta_x();
int16_t mouse_get_delta_y();
int8_t mouse_get_wheel();

} // namespace input
