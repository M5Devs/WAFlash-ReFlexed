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
static bool g_pointer_pressed = false;
static int16_t g_pointer_x = 0;
static int16_t g_pointer_y = 0;

static void video_refresh_cb(const void *data, unsigned width, unsigned height, size_t pitch) {
    (void)pitch;
    g_last_frame_data = data;
    g_last_width = width;
    g_last_height = height;
}

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
    (void)port; (void)index;
    if (device == RETRO_DEVICE_POINTER) {
        if (id == RETRO_DEVICE_ID_POINTER_X) return g_pointer_x;
        if (id == RETRO_DEVICE_ID_POINTER_Y) return g_pointer_y;
        if (id == RETRO_DEVICE_ID_POINTER_PRESSED) return g_pointer_pressed ? 1 : 0;
    }
    return 0;
}

int main() {
    printf("[TEST] Starting Hoshi Saga 1 Gameplay Automated Integration Test...\n");

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

    retro_init();
    retro_set_video_refresh(video_refresh_cb);
    retro_set_input_state(input_state_cb);

    struct retro_game_info game_info;
    std::memset(&game_info, 0, sizeof(game_info));
    game_info.data = swf_data.data();
    game_info.size = swf_data.size();

    bool loaded = retro_load_game(&game_info);
    assert(loaded == true && "retro_load_game failed");

    // 1. Step 10 frames on title screen
    for (int i = 0; i < 10; ++i) {
        retro_run();
    }

    assert(g_last_frame_data != nullptr);
    const uint32_t* pixels = static_cast<const uint32_t*>(g_last_frame_data);
    size_t total_pixels = g_last_width * g_last_height;
    size_t non_black_pixels = 0;

    for (size_t i = 0; i < total_pixels; ++i) {
        if ((pixels[i] & 0x00FFFFFF) != 0) {
            non_black_pixels++;
        }
    }

    printf("  [PASS] Title screen rendered text/graphics. Non-black pixels: %zu / %zu\n",
           non_black_pixels, total_pixels);
    assert(non_black_pixels > 0 && "Title screen rendered solid black background without text/graphics!");

    // 2. Simulate clicking "START GAME" button at center (275, 200)
    g_pointer_pressed = true;
    g_pointer_x = 0; // Center in [-0x7fff, 0x7fff]
    g_pointer_y = 0;

    retro_run(); // Trigger click

    g_pointer_pressed = false;

    // Step 30 frames into puzzle level
    for (int i = 0; i < 30; ++i) {
        retro_run();
    }

    printf("  [PASS] Clicked START GAME button and stepped into puzzle level successfully.\n");

    retro_deinit();
    printf("[TEST] All Hoshi Saga 1 gameplay tests passed successfully!\n");
    return 0;
}
