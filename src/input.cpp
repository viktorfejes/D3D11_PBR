#include "input.hpp"

#include <cassert>
#include <cstring>

// Input system's pointer gets stored here
// for performance reasons. This way, when
// input is polled it won't cause EXTRA lookup.
static Input *state_ptr = nullptr;
static KeyCode vk_to_key_code[256];

static void init_vk_map();

bool input::initialize(Input *state) {
    assert(state && "input::initialize: Input state pointer cannot be NULL");
    state_ptr = state;

    // Zero out the input system
    std::memset(state_ptr, 0, sizeof(Input));

    init_vk_map();

    return true;
}

void input::swap_buffers(Input *state) {
    assert(state && "input::swap_buffers: Input state pointer cannot be NULL");

    // Copy keyboard and mouse states to previous, and zeroing out mouse delta
    std::memcpy(&state->keyboard_previous, &state->keyboard_current, sizeof(KeyboardState));
    std::memcpy(&state->mouse_previous, &state->mouse_current, sizeof(MouseState));
    state->mouse_scroll_delta = 0;
}

void input::process_key(KeyCode key_code, bool pressed) {
    if (state_ptr->keyboard_current.keys[key_code] != pressed) {
        state_ptr->keyboard_current.keys[key_code] = pressed;
    }
}

void input::process_mouse_button(MouseButton button, bool pressed) {
    if (state_ptr->mouse_current.buttons[button] != pressed) {
        state_ptr->mouse_current.buttons[button] = pressed;
    }
}

void input::process_mouse_move(int16_t x, int16_t y) {
    // NOTE: This might not be necessary to be branched,
    // as this should only get called when the OS thinks
    // the mouse has moved, which hopefully is correct...
    if (state_ptr->mouse_current.x != x || state_ptr->mouse_current.y != y) {
        state_ptr->mouse_current.x = x;
        state_ptr->mouse_current.y = y;
    }
}

void input::process_mouse_wheel(int32_t delta) {
    state_ptr->mouse_scroll_delta = delta;
}

bool input::is_key_down(KeyCode key_code) {
    return state_ptr->keyboard_current.keys[key_code] == true;
}

bool input::is_key_up(KeyCode key_code) {
    return state_ptr->keyboard_current.keys[key_code] == false;
}

bool input::was_key_down(KeyCode key_code) {
    return state_ptr->keyboard_previous.keys[key_code] == true;
}

bool input::was_key_up(KeyCode key_code) {
    return state_ptr->keyboard_previous.keys[key_code] == false;
}

bool input::is_key_pressed(KeyCode key_code) {
    return was_key_up(key_code) && is_key_down(key_code);
}

bool input::is_key_released(KeyCode key_code) {
    return was_key_down(key_code) && is_key_up(key_code);
}

bool input::is_mouse_button_down(MouseButton button) {
    return state_ptr->mouse_current.buttons[button] == true;
}

bool input::is_mouse_button_up(MouseButton button) {
    return state_ptr->mouse_current.buttons[button] == false;
}

bool input::was_mouse_button_down(MouseButton button) {
    return state_ptr->mouse_previous.buttons[button] == true;
}

bool input::was_mouse_button_up(MouseButton button) {
    return state_ptr->mouse_previous.buttons[button] == false;
}

bool input::is_mouse_button_pressed(MouseButton button) {
    return was_mouse_button_up(button) && is_mouse_button_down(button);
}

bool input::is_mouse_button_released(MouseButton button) {
    return was_mouse_button_down(button) && is_mouse_button_up(button);
}

int16_t input::mouse_get_x() {
    return state_ptr->mouse_current.x;
}

int16_t input::mouse_get_y() {
    return state_ptr->mouse_current.y;
}

int16_t input::mouse_get_delta_x() {
    return state_ptr->mouse_current.x - state_ptr->mouse_previous.x;
}

int16_t input::mouse_get_delta_y() {
    return state_ptr->mouse_current.y - state_ptr->mouse_previous.y;
}

int8_t input::mouse_get_wheel() {
    return state_ptr->mouse_scroll_delta;
} 

static void init_vk_map() {
    std::memset(vk_to_key_code, KEY_UNKNOWN, sizeof(vk_to_key_code));
    vk_to_key_code['0'] = KEY_0;
}
