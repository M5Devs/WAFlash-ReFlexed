#include "wasm_api.h"
#include "base/tu_file.h"
#include "gameswf/gameswf.h"
#include "gameswf/gameswf_root.h"
#include "gameswf/gameswf_player.h"
#include "gameswf/gameswf_movie_def.h"
#include "gameswf/gameswf_types.h"
#include <vector>
#include <cstring>
#include <algorithm>

extern gameswf::render_handler* create_render_handler_wasm();
extern tu_file* create_gameswf_tu_file_mem(const uint8_t* data, size_t size);
namespace gameswf { void ensure_loaders_registered(); }

std::vector<uint32_t> g_wasm_framebuffer;
uint32_t              g_wasm_width  = 800;
uint32_t              g_wasm_height = 600;

static gameswf::player* g_player = nullptr;
static gameswf::root*   g_movie  = nullptr;

extern "C" {

int wasm_load_swf(const uint8_t* data, size_t size) {
    if (!data || size == 0) return 0;

    if (!g_player) {
        g_player = new gameswf::player();
        g_player->set_separate_thread(false);
    }

    gameswf::set_render_handler(create_render_handler_wasm());
    gameswf::set_sound_handler(nullptr);

    tu_file* in = create_gameswf_tu_file_mem(data, size);
    if (!in) return 0;

    gameswf::ensure_loaders_registered();

    gameswf::gc_ptr<gameswf::movie_def_impl> def = new gameswf::movie_def_impl(g_player, gameswf::DO_LOAD_BITMAPS, gameswf::DO_LOAD_FONT_SHAPES);
    def->read(in);
    delete in;

    g_movie = def->create_instance();
    if (!g_movie) return 0;

    int w = g_movie->get_movie_width();
    int h = g_movie->get_movie_height();
    g_wasm_width  = (w > 0) ? static_cast<uint32_t>(w) : 800;
    g_wasm_height = (h > 0) ? static_cast<uint32_t>(h) : 600;

    g_wasm_framebuffer.assign(g_wasm_width * g_wasm_height, 0xFFFFFFFF);

    return 1;
}

void wasm_step_frame(void) {
    if (!g_movie) return;

    float fps = g_movie->get_frame_rate();
    float delta_time = (fps > 0.0f) ? (1.0f / fps) : (1.0f / 30.0f);

    g_movie->advance(delta_time);
    g_movie->display();
}

const uint32_t* wasm_get_framebuffer(void) {
    return g_wasm_framebuffer.data();
}

int wasm_get_width(void)  { return static_cast<int>(g_wasm_width);  }
int wasm_get_height(void) { return static_cast<int>(g_wasm_height); }

void wasm_send_key(int keycode, int is_down) {
    if (g_movie && g_player) {
        g_movie->notify_key_event(g_player, static_cast<gameswf::key::code>(keycode), is_down != 0);
    }
}

void wasm_send_pointer(int x, int y, int is_down) {
    if (g_movie) {
        g_movie->notify_mouse_state(x, y, is_down ? 1 : 0);
    }
}

uint32_t wasm_get_player_score(void) { return 0; }
uint32_t wasm_get_player_hp(void)    { return 0; }

size_t wasm_get_audio_samples(int16_t* out_buffer, size_t num_frames) {
    if (!out_buffer || num_frames == 0) return 0;
    std::memset(out_buffer, 0, num_frames * 2 * sizeof(int16_t));
    return num_frames;
}

} // extern "C"
