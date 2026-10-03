#include "wasm_api.h"
#include "swf_parser.h"
#include "input_manager.h"
#include "avm2_vm.h"
#include "audio_mixer.h"
#include "retro_flash_memory.h"
#include "display_list.h"
#include <vector>
#include <cstring>
#include <algorithm>
#include <array>

static SWFParser             g_wasm_swf_parser;
static AVM2VM                g_wasm_avm2_vm;
static AudioMixer           g_wasm_audio_mixer;
static RetroFlashMemoryMap  g_wasm_memory_map;
static std::vector<uint32_t> g_wasm_framebuffer;
static uint32_t              g_wasm_width = 800;
static uint32_t              g_wasm_height = 600;
static uint32_t              g_wasm_current_frame = 0;
static uint32_t              g_wasm_total_frames = 1;
static bool                  g_wasm_game_loaded = false;

static std::array<bool, 256> g_wasm_key_states = {false};
static int32_t               g_wasm_mouse_x = 0;
static int32_t               g_wasm_mouse_y = 0;
static bool                  g_wasm_mouse_pressed = false;

static void render_character(const DisplayList& dl, uint16_t character_id, int32_t pos_x, int32_t pos_y) {
    const SWFShapeDefinition* shape = dl.find_shape(character_id);
    if (shape) {
        int32_t screen_x_min = shape->x_min + pos_x;
        int32_t screen_x_max = shape->x_max + pos_x;
        int32_t screen_y_min = shape->y_min + pos_y;
        int32_t screen_y_max = shape->y_max + pos_y;

        int32_t clip_x_start = std::max<int32_t>(0, screen_x_min);
        int32_t clip_x_end   = std::min<int32_t>(static_cast<int32_t>(g_wasm_width), screen_x_max);
        int32_t clip_y_start = std::max<int32_t>(0, screen_y_min);
        int32_t clip_y_end   = std::min<int32_t>(static_cast<int32_t>(g_wasm_height), screen_y_max);

        for (int32_t y = clip_y_start; y < clip_y_end; ++y) {
            for (int32_t x = clip_x_start; x < clip_x_end; ++x) {
                g_wasm_framebuffer[y * g_wasm_width + x] = shape->fill_color_xrgb;
            }
        }
        return;
    }

    const SWFSpriteDefinition* sprite = dl.find_sprite(character_id);
    if (sprite) {
        for (const auto& sub_obj : sprite->sub_objects) {
            render_character(dl, sub_obj.character_id, pos_x + sub_obj.transform_x, pos_y + sub_obj.transform_y);
        }
    }
}

extern "C" {

int wasm_load_swf(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return 0;
    }

    std::memset(&g_wasm_memory_map, 0, sizeof(RetroFlashMemoryMap));
    g_wasm_game_loaded = false;

    g_wasm_swf_parser.set_audio_mixer(&g_wasm_audio_mixer);

    if (!g_wasm_swf_parser.parse(data, size)) {
        return 0;
    }

    const SWFHeader& header = g_wasm_swf_parser.get_header();
    g_wasm_width = header.width_px ? header.width_px : 800;
    g_wasm_height = header.height_px ? header.height_px : 600;

    g_wasm_current_frame = 0;
    size_t show_frames = g_wasm_swf_parser.get_show_frame_count();
    g_wasm_total_frames = show_frames > 0 ? static_cast<uint32_t>(show_frames) :
                          (header.frame_count > 0 ? header.frame_count : 1);

    g_wasm_framebuffer.assign(g_wasm_width * g_wasm_height, header.background_color_xrgb);

    g_wasm_avm2_vm.reset();
    g_wasm_avm2_vm.set_retro_memory(&g_wasm_memory_map);
    g_wasm_audio_mixer.reset();

    g_wasm_game_loaded = true;
    return 1;
}

void wasm_step_frame(void) {
    if (!g_wasm_game_loaded) {
        return;
    }

    if (g_wasm_total_frames > 0) {
        g_wasm_current_frame = (g_wasm_current_frame + 1) % g_wasm_total_frames;
    }

    const SWFHeader& header = g_wasm_swf_parser.get_header();
    std::fill(g_wasm_framebuffer.begin(), g_wasm_framebuffer.end(), header.background_color_xrgb);

    const DisplayList& dl = g_wasm_swf_parser.get_display_list();
    for (const auto& pair : dl.get_active_objects()) {
        const DisplayObject& obj = pair.second;
        render_character(dl, obj.character_id, obj.transform_x, obj.transform_y);
    }
}

const uint32_t* wasm_get_framebuffer(void) {
    return g_wasm_framebuffer.data();
}

int wasm_get_width(void) {
    return static_cast<int>(g_wasm_width);
}

int wasm_get_height(void) {
    return static_cast<int>(g_wasm_height);
}

void wasm_send_key(int keycode, int is_down) {
    if (keycode >= 0 && keycode < 256) {
        g_wasm_key_states[keycode] = (is_down != 0);
    }
}

void wasm_send_pointer(int x, int y, int is_down) {
    g_wasm_mouse_x = x;
    g_wasm_mouse_y = y;
    g_wasm_mouse_pressed = (is_down != 0);
}

uint32_t wasm_get_player_score(void) {
    return g_wasm_memory_map.player_score;
}

uint32_t wasm_get_player_hp(void) {
    return g_wasm_memory_map.player_hp;
}

size_t wasm_get_audio_samples(int16_t* out_buffer, size_t num_frames) {
    if (!out_buffer || num_frames == 0) return 0;
    return g_wasm_audio_mixer.generate_audio_frame(out_buffer, num_frames);
}

}
