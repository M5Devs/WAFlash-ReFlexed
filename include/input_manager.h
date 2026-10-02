#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <cstdint>
#include <array>
#include "libretro.h"

class InputManager {
public:
    InputManager();
    ~InputManager();

    void poll_inputs(retro_input_state_t input_cb, uint32_t stage_width, uint32_t stage_height);

    bool is_key_down(uint32_t flash_keycode) const;
    int32_t get_mouse_x() const { return m_mouse_x; }
    int32_t get_mouse_y() const { return m_mouse_y; }
    bool is_mouse_down() const { return m_mouse_pressed; }

private:
    std::array<bool, 256> m_key_states;
    int32_t m_mouse_x;
    int32_t m_mouse_y;
    bool m_mouse_pressed;
};

#endif // INPUT_MANAGER_H
