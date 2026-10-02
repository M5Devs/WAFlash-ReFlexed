/**
 * libretro-flash Core Stub
 * Bridge interface connecting Libretro API callbacks with AVMPlus (AS3 Virtual Machine)
 * and Flare (SWF Vector Renderer) engines.
 */

#include "libretro.h"
#include "retro_flash_memory.h"
#include "swf_parser.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>

// Standard frame geometry defaults
#define CORE_DEFAULT_WIDTH  800
#define CORE_DEFAULT_HEIGHT 600
#define CORE_DEFAULT_FPS    60.0

// Simulated Memory Layout structure wrapping RetroFlashMemoryMap
struct SimulatedAVM3MemoryMap {
    RetroFlashMemoryMap system_ram; // Linear state space exposed to RETRO_MEMORY_SYSTEM_RAM
    bool                game_loaded;
};

// Global Core State
static struct {
    retro_environment_t        env_cb;
    retro_video_refresh_t      video_cb;
    retro_audio_sample_t       audio_cb;
    retro_audio_sample_batch_t audio_batch_cb;
    retro_input_poll_t         input_poll_cb;
    retro_input_state_t        input_state_cb;
    retro_log_printf_t         log_cb;

    std::vector<uint32_t>      m_framebuffer;
    size_t                     frame_buffer_pitch;
    SimulatedAVM3MemoryMap     avm_memory;
    bool                       initialized;

    uint32_t                   swf_width;
    uint32_t                   swf_height;
    float                      swf_fps;
    SWFParser                  swf_parser;
} g_core;

void retro_set_environment(retro_environment_t cb) {
    g_core.env_cb = cb;
    if (cb) {
        struct retro_log_callback log_cb;
        if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log_cb)) {
            g_core.log_cb = log_cb.log;
        }

        enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
        cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);

        bool support_achievements = true;
        cb(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &support_achievements);
    }
}

void retro_set_video_refresh(retro_video_refresh_t cb) { g_core.video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb)   { g_core.audio_cb = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { g_core.audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb)       { g_core.input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb)     { g_core.input_state_cb = cb; }

void retro_init(void) {
    g_core.swf_width = CORE_DEFAULT_WIDTH;
    g_core.swf_height = CORE_DEFAULT_HEIGHT;
    g_core.swf_fps = CORE_DEFAULT_FPS;

    g_core.m_framebuffer.resize(CORE_DEFAULT_WIDTH * CORE_DEFAULT_HEIGHT, 0x00FFFFFF);
    g_core.frame_buffer_pitch = CORE_DEFAULT_WIDTH * sizeof(uint32_t);
    memset(&g_core.avm_memory, 0, sizeof(SimulatedAVM3MemoryMap));
    g_core.initialized = true;

    if (g_core.log_cb) {
        g_core.log_cb(RETRO_LOG_INFO, "[libretro-flash] Core initialized successfully.\n");
    }
}

void retro_deinit(void) {
    g_core.m_framebuffer.clear();
    g_core.m_framebuffer.shrink_to_fit();
    g_core.initialized = false;
}

unsigned retro_api_version(void) {
    return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info *info) {
    memset(info, 0, sizeof(*info));
    info->library_name     = "WAFlash Re:Flexed Core";
    info->library_version  = "1.0.0";
    info->valid_extensions = "swf";
    info->need_fullpath    = false;
    info->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
    memset(info, 0, sizeof(*info));
    uint32_t w = g_core.swf_width ? g_core.swf_width : CORE_DEFAULT_WIDTH;
    uint32_t h = g_core.swf_height ? g_core.swf_height : CORE_DEFAULT_HEIGHT;
    float fps = g_core.swf_fps > 0.0f ? g_core.swf_fps : CORE_DEFAULT_FPS;

    info->geometry.base_width   = w;
    info->geometry.base_height  = h;
    info->geometry.max_width    = 1920;
    info->geometry.max_height   = 1080;
    info->geometry.aspect_ratio = (float)w / (float)h;

    info->timing.fps         = fps;
    info->timing.sample_rate = 44100.0;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
    (void)port;
    (void)device;
}

void retro_reset(void) {
    // Reset AVMPlus Virtual Machine and Flare display list to initial frame
    if (g_core.log_cb) {
        g_core.log_cb(RETRO_LOG_INFO, "[libretro-flash] Resetting AVMPlus runtime and Flare timeline.\n");
    }
}

bool retro_load_game(const struct retro_game_info *game) {
    if (!game || !game->data || game->size == 0) {
        return false;
    }

    if (g_core.log_cb) {
        g_core.log_cb(RETRO_LOG_INFO, "[libretro-flash] Loading SWF payload (%zu bytes).\n", game->size);
    }

    const uint8_t* swf_data = static_cast<const uint8_t*>(game->data);
    if (!g_core.swf_parser.parse(swf_data, game->size)) {
        if (g_core.log_cb) {
            g_core.log_cb(RETRO_LOG_ERROR, "[libretro-flash] Failed to parse SWF payload.\n");
        }
        return false;
    }

    const SWFHeader& header = g_core.swf_parser.get_header();
    g_core.swf_width = header.width_px ? header.width_px : CORE_DEFAULT_WIDTH;
    g_core.swf_height = header.height_px ? header.height_px : CORE_DEFAULT_HEIGHT;
    g_core.swf_fps = header.frame_rate > 0.0f ? header.frame_rate : CORE_DEFAULT_FPS;

    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (g_core.env_cb) {
        g_core.env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
    }

    // Reallocate frame buffer if dimensions change and clear with stage background color
    g_core.m_framebuffer.assign(g_core.swf_width * g_core.swf_height, header.background_color_xrgb);
    g_core.frame_buffer_pitch = g_core.swf_width * sizeof(uint32_t);

    if (g_core.log_cb) {
        g_core.log_cb(RETRO_LOG_INFO,
            "[libretro-flash] SWF Parsed: Version=%u, Sig=%c%c%c, Dim=%ux%u, FPS=%.2f, ABC Blocks=%zu\n",
            header.version, header.signature[0], header.signature[1], header.signature[2],
            g_core.swf_width, g_core.swf_height, g_core.swf_fps,
            g_core.swf_parser.get_abc_tags().size());
    }

    g_core.avm_memory.game_loaded = true;
    return true;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info) {
    (void)game_type;
    (void)info;
    (void)num_info;
    return false;
}

void retro_unload_game(void) {
    g_core.avm_memory.game_loaded = false;
}

void retro_run(void) {
    if (!g_core.avm_memory.game_loaded) return;

    if (g_core.input_poll_cb) {
        g_core.input_poll_cb();
    }

    const SWFHeader& header = g_core.swf_parser.get_header();
    std::fill(g_core.m_framebuffer.begin(), g_core.m_framebuffer.end(), header.background_color_xrgb);

    if (g_core.video_cb && !g_core.m_framebuffer.empty()) {
        g_core.video_cb(g_core.m_framebuffer.data(), g_core.swf_width, g_core.swf_height, g_core.frame_buffer_pitch);
    }

    if (g_core.audio_batch_cb) {
        size_t frames = static_cast<size_t>(44100.0f / (g_core.swf_fps > 0.0f ? g_core.swf_fps : 60.0f));
        if (frames > 1024) frames = 1024;
        int16_t silence[2048] = {0};
        g_core.audio_batch_cb(silence, frames);
    }
}

unsigned retro_get_region(void) {
    return RETRO_REGION_NTSC;
}

size_t retro_serialize_size(void) {
    return sizeof(SimulatedAVM3MemoryMap);
}

bool retro_serialize(void *data, size_t size) {
    if (size < sizeof(SimulatedAVM3MemoryMap)) return false;
    memcpy(data, &g_core.avm_memory, sizeof(SimulatedAVM3MemoryMap));
    return true;
}

bool retro_unserialize(const void *data, size_t size) {
    if (size < sizeof(SimulatedAVM3MemoryMap)) return false;
    memcpy(&g_core.avm_memory, data, sizeof(SimulatedAVM3MemoryMap));
    return true;
}

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {
    (void)index;
    (void)enabled;
    (void)code;
}

void *retro_get_memory_data(unsigned id) {
    switch (id) {
        case RETRO_MEMORY_SYSTEM_RAM:
            return &g_core.avm_memory.system_ram;
        default:
            return NULL;
    }
}

size_t retro_get_memory_size(unsigned id) {
    switch (id) {
        case RETRO_MEMORY_SYSTEM_RAM:
            return sizeof(g_core.avm_memory.system_ram);
        default:
            return 0;
    }
}
