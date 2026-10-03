/**
 * libretro-flash Core Stub
 * Bridge interface connecting Libretro API callbacks with AVMPlus (AS3 Virtual Machine)
 * and Flare (SWF Vector Renderer) engines.
 */

#include "libretro.h"
#include "retro_flash_memory.h"
#include "swf_parser.h"
#include "input_manager.h"
#include "avm2_vm.h"
#include "display_list.h"
#include "audio_mixer.h"
#include "serialization.h"
#include "vector_rasterizer.h"
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

// Core Options configuration settings
static struct {
    unsigned resolution_scale; // 1, 2, or 4
    unsigned framerate_mode;   // 0: Auto, 1: 30 FPS, 2: 60 FPS
    bool ra_sync_enabled;      // RetroAchievements Memory Mirroring
} g_opts = { 1, 0, true };

// Core Options Definition (V2 & V1 Fallback)
static struct retro_core_option_v2_definition g_core_options_v2[] = {
    {
        "waflash_resolution_scale",
        "Internal Resolution Scale",
        NULL,
        "Select internal render scale multiplier.",
        NULL,
        {
            { "1x (Original)", "1x (Original)" },
            { "2x (HD)", "2x (HD)" },
            { "4x (Ultra HD)", "4x (Ultra HD)" },
            { NULL, NULL }
        },
        "1x (Original)"
    },
    {
        "waflash_framerate_mode",
        "Framerate Mode",
        NULL,
        "Select timeline frame rate operation mode.",
        NULL,
        {
            { "Auto (Match SWF)", "Auto (Match SWF)" },
            { "Fixed 30 FPS", "Fixed 30 FPS" },
            { "Fixed 60 FPS", "Fixed 60 FPS" },
            { NULL, NULL }
        },
        "Auto (Match SWF)"
    },
    {
        "waflash_retroachievements_sync",
        "RetroAchievements Memory Mirroring",
        NULL,
        "Enable or disable memory mapping synchronization for RetroAchievements.",
        NULL,
        {
            { "Enabled", "Enabled" },
            { "Disabled", "Disabled" },
            { NULL, NULL }
        },
        "Enabled"
    },
    { NULL, NULL, NULL, NULL, NULL, { { NULL, NULL } }, NULL }
};

static struct retro_variable g_core_variables_v1[] = {
    { "waflash_resolution_scale", "Internal Resolution Scale; 1x (Original)|2x (HD)|4x (Ultra HD)" },
    { "waflash_framerate_mode", "Framerate Mode; Auto (Match SWF)|Fixed 30 FPS|Fixed 60 FPS" },
    { "waflash_retroachievements_sync", "RetroAchievements Memory Mirroring; Enabled|Disabled" },
    { NULL, NULL }
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
    InputManager               input_manager;
    AVM2VM                     avm2_vm;
    AudioMixer                 audio_mixer;
    uint32_t                   m_current_frame;
    uint32_t                   m_total_frames;
} g_core;

static void update_core_options(void);
static void render_current_video_frame(void);

void retro_set_environment(retro_environment_t cb) {
    g_core.env_cb = cb;
    if (cb) {
        struct retro_log_callback log_cb;
        if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log_cb)) {
            g_core.log_cb = log_cb.log;
        }

        struct retro_core_options_v2 core_options_v2;
        core_options_v2.definitions = g_core_options_v2;
        if (!cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &core_options_v2)) {
            cb(RETRO_ENVIRONMENT_SET_VARIABLES, g_core_variables_v1);
        }

        enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
        cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);

        bool support_achievements = true;
        cb(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &support_achievements);

        update_core_options();
    }
}

static void update_core_options(void) {
    if (!g_core.env_cb) return;

    struct retro_variable var;

    var.key = "waflash_resolution_scale";
    if (g_core.env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
        if (strcmp(var.value, "2x (HD)") == 0) {
            g_opts.resolution_scale = 2;
        } else if (strcmp(var.value, "4x (Ultra HD)") == 0) {
            g_opts.resolution_scale = 4;
        } else {
            g_opts.resolution_scale = 1;
        }
    }

    var.key = "waflash_framerate_mode";
    if (g_core.env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
        if (strcmp(var.value, "Fixed 30 FPS") == 0) {
            g_opts.framerate_mode = 1;
        } else if (strcmp(var.value, "Fixed 60 FPS") == 0) {
            g_opts.framerate_mode = 2;
        } else {
            g_opts.framerate_mode = 0; // Auto
        }
    }

    var.key = "waflash_retroachievements_sync";
    if (g_core.env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
        if (strcmp(var.value, "Disabled") == 0) {
            g_opts.ra_sync_enabled = false;
        } else {
            g_opts.ra_sync_enabled = true;
        }
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
    g_core.m_current_frame = 0;
    g_core.m_total_frames = 1;

    g_core.m_framebuffer.resize(CORE_DEFAULT_WIDTH * CORE_DEFAULT_HEIGHT, 0x00FFFFFF);
    g_core.frame_buffer_pitch = CORE_DEFAULT_WIDTH * sizeof(uint32_t);
    memset(&g_core.avm_memory, 0, sizeof(SimulatedAVM3MemoryMap));
    g_core.avm2_vm.reset();
    g_core.avm2_vm.set_retro_memory(&g_core.avm_memory.system_ram);
    g_core.audio_mixer.reset();
    g_core.swf_parser.set_audio_mixer(&g_core.audio_mixer);
    g_core.initialized = true;

    if (g_core.log_cb) {
        g_core.log_cb(RETRO_LOG_INFO, "[libretro-flash] Core initialized successfully.\n");
    }
}

void retro_deinit(void) {
    g_core.m_framebuffer.clear();
    g_core.m_framebuffer.shrink_to_fit();
    g_core.audio_mixer.reset();
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

    if (g_opts.framerate_mode == 1) {
        fps = 30.0f;
    } else if (g_opts.framerate_mode == 2) {
        fps = 60.0f;
    }

    info->geometry.base_width   = w * g_opts.resolution_scale;
    info->geometry.base_height  = h * g_opts.resolution_scale;
    info->geometry.max_width    = 1920 * 4;
    info->geometry.max_height   = 1080 * 4;
    info->geometry.aspect_ratio = (float)w / (float)h;

    info->timing.fps         = fps;
    info->timing.sample_rate = 44100.0;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
    (void)port;
    (void)device;
}

void retro_reset(void) {
    g_core.m_current_frame = 0;
    g_core.avm2_vm.reset();
    g_core.audio_mixer.reset();
    if (g_core.log_cb) {
        g_core.log_cb(RETRO_LOG_INFO, "[libretro-flash] Resetting AVMPlus runtime and Flare timeline to frame 0.\n");
    }
}

bool retro_load_game(const struct retro_game_info *game) {
    if (!game || !game->data || game->size == 0) {
        return false;
    }

    if (g_core.log_cb) {
        g_core.log_cb(RETRO_LOG_INFO, "[libretro-flash] Loading SWF payload (%zu bytes).\n", game->size);
    }

    g_core.swf_parser.set_audio_mixer(&g_core.audio_mixer);

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

    g_core.m_current_frame = 0;
    size_t show_frame_count = g_core.swf_parser.get_show_frame_count();
    g_core.m_total_frames = show_frame_count > 0 ? static_cast<uint32_t>(show_frame_count) :
                            (header.frame_count > 0 ? header.frame_count : 1);

    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (g_core.env_cb) {
        g_core.env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
    }

    g_core.m_framebuffer.assign(g_core.swf_width * g_core.swf_height, header.background_color_xrgb);
    g_core.frame_buffer_pitch = g_core.swf_width * sizeof(uint32_t);

    g_core.avm2_vm.reset();
    g_core.avm2_vm.set_retro_memory(&g_core.avm_memory.system_ram);

    g_core.audio_mixer.reset();

    if (g_core.log_cb) {
        g_core.log_cb(RETRO_LOG_INFO,
            "[libretro-flash] SWF Parsed: Version=%u, Sig=%c%c%c, Dim=%ux%u, FPS=%.2f, ABC Blocks=%zu, Total Frames=%u\n",
            header.version, header.signature[0], header.signature[1], header.signature[2],
            g_core.swf_width, g_core.swf_height, g_core.swf_fps,
            g_core.swf_parser.get_abc_tags().size(), g_core.m_total_frames);
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
    g_core.m_current_frame = 0;
    g_core.avm2_vm.reset();
    g_core.audio_mixer.reset();
}

static void render_node_recursive(const DisplayList& dl, const std::shared_ptr<DisplayObjectNode>& node, const Matrix2D& parent_world) {
    if (!node) return;

    Matrix2D world = Matrix2D::multiply(parent_world, node->local_matrix);

    if (node->get_type() == DisplayObjectType::Shape) {
        const SWFShapeDefinition* shape = dl.find_shape(node->character_id);
        if (shape) {
            std::vector<Point2D> world_verts;
            if (!shape->polygon_vertices.empty()) {
                world_verts.reserve(shape->polygon_vertices.size());
                for (const auto& v : shape->polygon_vertices) {
                    float xp = world.a * v.x + world.c * v.y + world.tx;
                    float yp = world.b * v.x + world.d * v.y + world.ty;
                    world_verts.push_back({xp, yp});
                }
            } else {
                float x1 = static_cast<float>(shape->x_min);
                float x2 = static_cast<float>(shape->x_max);
                float y1 = static_cast<float>(shape->y_min);
                float y2 = static_cast<float>(shape->y_max);
                Point2D local_rect[4] = {{x1, y1}, {x2, y1}, {x2, y2}, {x1, y2}};
                for (int i = 0; i < 4; ++i) {
                    float xp = world.a * local_rect[i].x + world.c * local_rect[i].y + world.tx;
                    float yp = world.b * local_rect[i].x + world.d * local_rect[i].y + world.ty;
                    world_verts.push_back({xp, yp});
                }
            }

            VectorTessellator::rasterize_polygon(
                world_verts,
                shape->fill_color_xrgb,
                g_core.m_framebuffer.data(),
                g_core.swf_width,
                g_core.swf_height
            );
        }
    } else if (node->get_type() == DisplayObjectType::MovieClip) {
        auto clip = std::static_pointer_cast<MovieClipInstance>(node);
        for (const auto& [depth, child] : clip->children) {
            render_node_recursive(dl, child, world);
        }
    }
}

static void render_current_video_frame(void) {
    const SWFHeader& header = g_core.swf_parser.get_header();
    std::fill(g_core.m_framebuffer.begin(), g_core.m_framebuffer.end(), header.background_color_xrgb);

    const DisplayList& dl = g_core.swf_parser.get_display_list();
    Matrix2D identity_matrix{};
    if (dl.get_root_stage()) {
        for (const auto& [depth, child] : dl.get_root_stage()->children) {
            render_node_recursive(dl, child, identity_matrix);
        }
    }

    if (g_core.video_cb && !g_core.m_framebuffer.empty()) {
        g_core.video_cb(g_core.m_framebuffer.data(), g_core.swf_width, g_core.swf_height, g_core.frame_buffer_pitch);
    }
}

void retro_run(void) {
    if (!g_core.avm_memory.game_loaded) return;

    bool updated = false;
    if (g_core.env_cb && g_core.env_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) {
        update_core_options();
    }

    if (g_core.input_poll_cb) {
        g_core.input_poll_cb();
    }

    g_core.input_manager.poll_inputs(g_core.input_state_cb, g_core.swf_width, g_core.swf_height);

    if (g_core.m_total_frames > 0) {
        g_core.m_current_frame = (g_core.m_current_frame + 1) % g_core.m_total_frames;
    }

    // Recursively advance nested MovieClips
    g_core.swf_parser.get_display_list().advance_frame();

    render_current_video_frame();

    if (g_core.audio_batch_cb) {
        size_t frames = static_cast<size_t>(44100.0f / (g_core.swf_fps > 0.0f ? g_core.swf_fps : 60.0f));
        if (frames > 1024) frames = 1024;
        std::vector<int16_t> pcm_buffer(frames * AudioMixer::CHANNELS);
        g_core.audio_mixer.generate_audio_frame(pcm_buffer.data(), frames);
        g_core.audio_batch_cb(pcm_buffer.data(), frames);
    }
}

unsigned retro_get_region(void) {
    return RETRO_REGION_NTSC;
}

size_t retro_serialize_size(void) {
    return sizeof(CoreStatePayload);
}

bool retro_serialize(void *data, size_t size) {
    if (!data || size < sizeof(CoreStatePayload)) return false;

    CoreStatePayload* payload = static_cast<CoreStatePayload*>(data);
    std::memset(payload, 0, sizeof(CoreStatePayload));

    payload->header.magic = WAFLASH_SERIALIZE_MAGIC;
    payload->header.version = WAFLASH_SERIALIZE_VERSION;
    payload->header.current_frame = g_core.m_current_frame;

    std::memcpy(&payload->memory_map, &g_core.avm_memory.system_ram, sizeof(RetroFlashMemoryMap));

    g_core.avm2_vm.export_state(payload->avm2_state);
    g_core.swf_parser.get_display_list().export_state(payload->display_list_state);

    return true;
}

bool retro_unserialize(const void *data, size_t size) {
    if (!data || size < sizeof(CoreStatePayload)) return false;

    const CoreStatePayload* payload = static_cast<const CoreStatePayload*>(data);

    if (payload->header.magic != WAFLASH_SERIALIZE_MAGIC || payload->header.version != WAFLASH_SERIALIZE_VERSION) {
        return false;
    }

    g_core.m_current_frame = payload->header.current_frame;
    std::memcpy(&g_core.avm_memory.system_ram, &payload->memory_map, sizeof(RetroFlashMemoryMap));

    g_core.avm2_vm.import_state(payload->avm2_state);
    g_core.swf_parser.get_display_list().import_state(payload->display_list_state);

    render_current_video_frame();

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
