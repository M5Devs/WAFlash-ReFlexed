#include "wasm_api.h"
#include <cassert>
#include <cstdio>
#include <vector>

int main() {
    printf("[TEST] Starting WASM C-API Unit Tests...\n");

    // Minimal FWS SWF header (8 bytes) + Rect (5 bytes) + FrameRate/Count + TagEnd
    std::vector<uint8_t> dummy_swf = {
        'F', 'W', 'S', 15,
        18, 0, 0, 0, // File length = 18 bytes
        0x08, 0x00, 0x00, 0x00, 0x00, // Rect 0x0 twips
        0x00, 0x3C, // 60 fps
        0x01, 0x00, // 1 frame
        0x00, 0x00  // TagEnd
    };

    int loaded = wasm_load_swf(dummy_swf.data(), dummy_swf.size());
    assert(loaded == 1);
    printf("  [PASS] wasm_load_swf successfully loaded valid SWF buffer.\n");

    int w = wasm_get_width();
    int h = wasm_get_height();
    assert(w >= 0 && h >= 0);
    printf("  [PASS] Dimensions obtained: %dx%d\n", w, h);

    const uint32_t* fb = wasm_get_framebuffer();
    assert(fb != nullptr);
    printf("  [PASS] Framebuffer pointer valid.\n");

    wasm_step_frame();
    printf("  [PASS] wasm_step_frame executed successfully.\n");

    wasm_send_key(65, 1);
    wasm_send_key(65, 0);
    wasm_send_pointer(100, 200, 1);
    wasm_send_pointer(100, 200, 0);
    printf("  [PASS] Input functions wasm_send_key and wasm_send_pointer called without error.\n");

    uint32_t score = wasm_get_player_score();
    uint32_t hp = wasm_get_player_hp();
    assert(score == 0 && hp == 0);
    printf("  [PASS] HUD score & HP getters return valid memory values (%u, %u).\n", score, hp);

    printf("[TEST] All WASM C-API unit tests passed successfully!\n");
    return 0;
}
