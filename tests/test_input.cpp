#include "input_manager.h"
#include <cassert>
#include <cstdio>
#include <cmath>

static unsigned g_active_joypad_id = 999;
static int16_t g_ptr_x = 0;
static int16_t g_ptr_y = 0;
static int16_t g_ptr_pressed = 0;
static int16_t g_mouse_dx = 0;
static int16_t g_mouse_dy = 0;
static int16_t g_mouse_btn = 0;

static int16_t mock_input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
    (void)port;
    (void)index;
    if (device == RETRO_DEVICE_JOYPAD) {
        return (id == g_active_joypad_id) ? 1 : 0;
    }
    if (device == RETRO_DEVICE_POINTER) {
        if (id == RETRO_DEVICE_ID_POINTER_X) return g_ptr_x;
        if (id == RETRO_DEVICE_ID_POINTER_Y) return g_ptr_y;
        if (id == RETRO_DEVICE_ID_POINTER_PRESSED) return g_ptr_pressed;
    }
    if (device == RETRO_DEVICE_MOUSE) {
        if (id == RETRO_DEVICE_ID_MOUSE_X) return g_mouse_dx;
        if (id == RETRO_DEVICE_ID_MOUSE_Y) return g_mouse_dy;
        if (id == RETRO_DEVICE_ID_MOUSE_LEFT) return g_mouse_btn;
    }
    return 0;
}

int main() {
    printf("[TEST] Starting Input Subsystem (RetroPad & Pointer/Mouse) Unit Tests...\n");

    InputManager input;

    // --- TEST 1: RetroPad Joypad Keycode Mappings ---
    struct KeyMapping {
        unsigned retro_id;
        uint32_t flash_code1;
        uint32_t flash_code2;
    };

    KeyMapping mappings[] = {
        { RETRO_DEVICE_ID_JOYPAD_UP,    38, 87 },
        { RETRO_DEVICE_ID_JOYPAD_DOWN,  40, 83 },
        { RETRO_DEVICE_ID_JOYPAD_LEFT,  37, 65 },
        { RETRO_DEVICE_ID_JOYPAD_RIGHT, 39, 68 },
        { RETRO_DEVICE_ID_JOYPAD_A,     90, 32 },
        { RETRO_DEVICE_ID_JOYPAD_B,     88, 0  },
        { RETRO_DEVICE_ID_JOYPAD_X,     67, 0  },
        { RETRO_DEVICE_ID_JOYPAD_Y,     16, 0  },
        { RETRO_DEVICE_ID_JOYPAD_START, 13, 0  }
    };

    for (const auto& m : mappings) {
        g_active_joypad_id = m.retro_id;
        g_ptr_pressed = 0;
        g_mouse_btn = 0;

        input.poll_inputs(mock_input_state_cb, 800, 600);

        assert(input.is_key_down(m.flash_code1) == true);
        if (m.flash_code2 != 0) {
            assert(input.is_key_down(m.flash_code2) == true);
        }
    }
    printf("  [PASS] All RetroPad Joypad to Flash KeyCode translations verified successfully.\n");

    // --- TEST 2: Pointer Coordinate Scaling ---
    g_active_joypad_id = 999;
    g_ptr_pressed = 1;

    // Test Center (0, 0) in [-32767, 32767] -> Stage (400, 300)
    g_ptr_x = 0;
    g_ptr_y = 0;
    input.poll_inputs(mock_input_state_cb, 800, 600);

    assert(input.is_mouse_down() == true);
    assert(std::abs(input.get_mouse_x() - 400) <= 1);
    assert(std::abs(input.get_mouse_y() - 300) <= 1);

    // Test Top-Left (-32767, -32767) -> Stage (0, 0)
    g_ptr_x = -32767;
    g_ptr_y = -32767;
    input.poll_inputs(mock_input_state_cb, 800, 600);

    assert(input.is_mouse_down() == true);
    assert(input.get_mouse_x() == 0);
    assert(input.get_mouse_y() == 0);

    // Test Bottom-Right (32767, 32767) -> Stage (800, 600)
    g_ptr_x = 32767;
    g_ptr_y = 32767;
    input.poll_inputs(mock_input_state_cb, 800, 600);

    assert(input.is_mouse_down() == true);
    assert(input.get_mouse_x() == 800);
    assert(input.get_mouse_y() == 600);

    printf("  [PASS] Pointer coordinate scaling to stage dimensions verified successfully.\n");

    // --- TEST 3: Mouse Relative Movement & Click ---
    g_ptr_pressed = 0;
    g_mouse_dx = 15;
    g_mouse_dy = 25;
    g_mouse_btn = 1;

    input.poll_inputs(mock_input_state_cb, 800, 600);

    assert(input.is_mouse_down() == true);
    assert(input.get_mouse_x() == 800 + 15 || input.get_mouse_x() == 800); // clamped to 800
    assert(input.get_mouse_y() == 600);

    printf("  [PASS] Relative Mouse delta and button state verified successfully.\n");

    printf("[TEST] All Input Subsystem unit tests passed successfully!\n");
    return 0;
}
