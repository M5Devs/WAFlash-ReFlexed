#ifndef WASM_API_H
#define WASM_API_H

#include <cstdint>
#include <cstddef>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#ifndef EMSCRIPTEN_KEEPALIVE
#define EMSCRIPTEN_KEEPALIVE
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

EMSCRIPTEN_KEEPALIVE int wasm_load_swf(const uint8_t* data, size_t size);
EMSCRIPTEN_KEEPALIVE void wasm_step_frame(void);
EMSCRIPTEN_KEEPALIVE const uint32_t* wasm_get_framebuffer(void);
EMSCRIPTEN_KEEPALIVE int wasm_get_width(void);
EMSCRIPTEN_KEEPALIVE int wasm_get_height(void);
EMSCRIPTEN_KEEPALIVE void wasm_send_key(int keycode, int is_down);
EMSCRIPTEN_KEEPALIVE void wasm_send_pointer(int x, int y, int is_down);
EMSCRIPTEN_KEEPALIVE uint32_t wasm_get_player_score(void);
EMSCRIPTEN_KEEPALIVE uint32_t wasm_get_player_hp(void);

#ifdef __cplusplus
}
#endif

#endif // WASM_API_H
