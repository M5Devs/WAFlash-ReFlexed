#include "input_manager.h"
#include <algorithm>

InputManager::InputManager()
    : m_mouse_x(0), m_mouse_y(0), m_mouse_pressed(false) {
    m_key_states.fill(false);
}

InputManager::~InputManager() {}

bool InputManager::is_key_down(uint32_t flash_keycode) const {
    if (flash_keycode < 256) {
        return m_key_states[flash_keycode];
    }
    return false;
}

void InputManager::poll_inputs(retro_input_state_t input_cb, uint32_t stage_width, uint32_t stage_height) {
    m_key_states.fill(false);
    m_mouse_pressed = false;

    if (!input_cb) {
        return;
    }

    // 1. Poll RetroPad Joypad buttons
    // RETRO_DEVICE_ID_JOYPAD_UP    -> KeyCode 38 (Up) & 87 ('W')
    if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP)) {
        m_key_states[38] = true;
        m_key_states[87] = true;
    }
    // RETRO_DEVICE_ID_JOYPAD_DOWN  -> KeyCode 40 (Down) & 83 ('S')
    if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN)) {
        m_key_states[40] = true;
        m_key_states[83] = true;
    }
    // RETRO_DEVICE_ID_JOYPAD_LEFT  -> KeyCode 37 (Left) & 65 ('A')
    if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT)) {
        m_key_states[37] = true;
        m_key_states[65] = true;
    }
    // RETRO_DEVICE_ID_JOYPAD_RIGHT -> KeyCode 39 (Right) & 68 ('D')
    if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT)) {
        m_key_states[39] = true;
        m_key_states[68] = true;
    }
    // RETRO_DEVICE_ID_JOYPAD_A     -> KeyCode 90 ('Z') & 32 (Space)
    if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A)) {
        m_key_states[90] = true;
        m_key_states[32] = true;
    }
    // RETRO_DEVICE_ID_JOYPAD_B     -> KeyCode 88 ('X')
    if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B)) {
        m_key_states[88] = true;
    }
    // RETRO_DEVICE_ID_JOYPAD_X     -> KeyCode 67 ('C')
    if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X)) {
        m_key_states[67] = true;
    }
    // RETRO_DEVICE_ID_JOYPAD_Y     -> KeyCode 16 (Shift)
    if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y)) {
        m_key_states[16] = true;
    }
    // RETRO_DEVICE_ID_JOYPAD_START -> KeyCode 13 (Enter)
    if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START)) {
        m_key_states[13] = true;
    }

    // 2. Poll Pointer & Mouse Inputs
    // Check pointer (absolute coordinates in [-0x7fff, 0x7fff])
    int16_t ptr_x = input_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
    int16_t ptr_y = input_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
    int16_t ptr_pressed = input_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED);

    if (ptr_pressed) {
        m_mouse_pressed = true;

        float norm_x = static_cast<float>(ptr_x + 0x7fff) / 65534.0f;
        float norm_y = static_cast<float>(ptr_y + 0x7fff) / 65534.0f;

        norm_x = std::clamp(norm_x, 0.0f, 1.0f);
        norm_y = std::clamp(norm_y, 0.0f, 1.0f);

        m_mouse_x = static_cast<int32_t>(norm_x * static_cast<float>(stage_width));
        m_mouse_y = static_cast<int32_t>(norm_y * static_cast<float>(stage_height));
    } else {
        // Check relative mouse inputs
        int16_t mouse_dx = input_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
        int16_t mouse_dy = input_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
        int16_t mouse_btn = input_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT);

        m_mouse_x = std::clamp(m_mouse_x + static_cast<int32_t>(mouse_dx), 0, static_cast<int32_t>(stage_width));
        m_mouse_y = std::clamp(m_mouse_y + static_cast<int32_t>(mouse_dy), 0, static_cast<int32_t>(stage_height));

        if (mouse_btn) {
            m_mouse_pressed = true;
        }
    }
}
