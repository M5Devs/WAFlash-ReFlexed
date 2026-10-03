#include "swf_parser.h"
#include "libretro.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

static const void* g_last_frame_data = nullptr;
static unsigned g_last_width = 0;
static unsigned g_last_height = 0;

static void video_refresh_callback(const void *data, unsigned width, unsigned height, size_t pitch) {
    (void)pitch;
    g_last_frame_data = data;
    g_last_width = width;
    g_last_height = height;
}

int main() {
    printf("[TEST] Starting Hoshi Saga 1 Real-World Flash Integration Test...\n");

    const char* filepaths[] = {
        "tests/fixtures/hoshi1.swf",
        "../tests/fixtures/hoshi1.swf",
        "../../tests/fixtures/hoshi1.swf"
    };

    std::ifstream file;
    for (const char* path : filepaths) {
        file.open(path, std::ios::binary | std::ios::ate);
        if (file.is_open()) break;
    }
    assert(file.is_open() && "Failed to open tests/fixtures/hoshi1.swf");

    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> swf_data(file_size);
    bool read_success = static_cast<bool>(file.read(reinterpret_cast<char*>(swf_data.data()), file_size));
    assert(read_success && "Failed to read SWF data");

    // 1. SWFParser Verification
    SWFParser parser;
    bool parsed = parser.parse(swf_data.data(), swf_data.size());
    assert(parsed == true && "SWFParser failed to parse hoshi1.swf");

    const SWFHeader& header = parser.get_header();
    assert(header.width_px == 550 && "Expected width 550px");
    assert(header.height_px == 400 && "Expected height 400px");
    assert(header.frame_rate == 30.0f && "Expected frame rate 30.0 fps");
    printf("  [PASS] SWF Header decoded: %ux%u @ %.2f FPS, Total Frames=%zu\n",
           header.width_px, header.height_px, header.frame_rate, parser.get_show_frame_count());

    // 2. Libretro Core Lifecycle & Frame Stepping
    retro_init();
    retro_set_video_refresh(video_refresh_callback);

    struct retro_game_info game_info;
    std::memset(&game_info, 0, sizeof(game_info));
    game_info.data = swf_data.data();
    game_info.size = swf_data.size();

    bool loaded = retro_load_game(&game_info);
    assert(loaded == true && "retro_load_game failed for hoshi1.swf");

    int frames_with_rendered_pixels = 0;
    const int total_ticks = 60;

    for (int tick = 0; tick < total_ticks; ++tick) {
        retro_run();

        assert(g_last_width == 550);
        assert(g_last_height == 400);
        assert(g_last_frame_data != nullptr);

        const uint32_t* pixels = static_cast<const uint32_t*>(g_last_frame_data);
        uint32_t top_left_bg = pixels[0];
        size_t total_pixel_count = g_last_width * g_last_height;
        size_t non_bg_pixels = 0;

        for (size_t i = 0; i < total_pixel_count; ++i) {
            if (pixels[i] != top_left_bg) {
                non_bg_pixels++;
            }
        }

        if (non_bg_pixels > 0) {
            frames_with_rendered_pixels++;
        }
    }

    printf("  [PASS] Executed %d frames. Non-blank rendered frames count: %d / %d.\n",
           total_ticks, frames_with_rendered_pixels, total_ticks);

    assert(frames_with_rendered_pixels > 0 && "Framebuffer was blank/solid color for all 60 frames!");

    retro_deinit();
    printf("[TEST] All Hoshi Saga 1 integration tests passed successfully!\n");
    return 0;
}
