#include "swf_parser.h"
#include "libretro.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

class BitWriter {
public:
    void write_bits(uint32_t val, uint8_t n) {
        for (int i = n - 1; i >= 0; --i) {
            uint8_t bit = (val >> i) & 1;
            current_byte = (current_byte << 1) | bit;
            bit_count++;
            if (bit_count == 8) {
                buffer.push_back(current_byte);
                current_byte = 0;
                bit_count = 0;
            }
        }
    }

    void align_byte() {
        if (bit_count > 0) {
            current_byte <<= (8 - bit_count);
            buffer.push_back(current_byte);
            current_byte = 0;
            bit_count = 0;
        }
    }

    const std::vector<uint8_t>& get_bytes() const { return buffer; }

private:
    std::vector<uint8_t> buffer;
    uint8_t current_byte = 0;
    uint8_t bit_count = 0;
};

static std::vector<uint8_t> create_rect_bytes(int32_t xmin, int32_t xmax, int32_t ymin, int32_t ymax) {
    BitWriter writer;
    uint8_t nbits = 15;
    writer.write_bits(nbits, 5);
    writer.write_bits(static_cast<uint32_t>(xmin) & 0x7FFF, nbits);
    writer.write_bits(static_cast<uint32_t>(xmax) & 0x7FFF, nbits);
    writer.write_bits(static_cast<uint32_t>(ymin) & 0x7FFF, nbits);
    writer.write_bits(static_cast<uint32_t>(ymax) & 0x7FFF, nbits);
    writer.align_byte();
    return writer.get_bytes();
}

static uint32_t g_last_frame_color = 0;
static unsigned g_last_width = 0;
static unsigned g_last_height = 0;

static void mock_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch) {
    (void)pitch;
    g_last_width = width;
    g_last_height = height;
    if (data && width > 0 && height > 0) {
        const uint32_t* pixels = static_cast<const uint32_t*>(data);
        g_last_frame_color = pixels[0];
    }
}

int main() {
    printf("[TEST] Starting Video Framebuffer & SWF Tag 9 Unit Tests...\n");

    // 1. Construct SWF with TagSetBackgroundColor (Tag 9) RGB (0x12, 0x34, 0x56)
    std::vector<uint8_t> rect = create_rect_bytes(0, 640 * 20, 0, 480 * 20); // 640x480

    std::vector<uint8_t> body_payload;
    body_payload.insert(body_payload.end(), rect.begin(), rect.end());

    // FrameRate: 30.0 fps (0x00 frac, 0x1E int)
    body_payload.push_back(0x00);
    body_payload.push_back(0x1E);

    // FrameCount: 1 frame
    body_payload.push_back(0x01);
    body_payload.push_back(0x00);

    // TagSetBackgroundColor (Tag 9, len = 3): Header = (9 << 6) | 3
    uint16_t tag9_header = (9 << 6) | 3;
    body_payload.push_back(tag9_header & 0xFF);
    body_payload.push_back((tag9_header >> 8) & 0xFF);
    body_payload.push_back(0x12); // R
    body_payload.push_back(0x34); // G
    body_payload.push_back(0x56); // B

    // TagEnd
    body_payload.push_back(0x00);
    body_payload.push_back(0x00);

    uint32_t total_uncompressed_len = static_cast<uint32_t>(8 + body_payload.size());

    std::vector<uint8_t> swf_data;
    swf_data.push_back('F');
    swf_data.push_back('W');
    swf_data.push_back('S');
    swf_data.push_back(15);
    swf_data.push_back(total_uncompressed_len & 0xFF);
    swf_data.push_back((total_uncompressed_len >> 8) & 0xFF);
    swf_data.push_back((total_uncompressed_len >> 16) & 0xFF);
    swf_data.push_back((total_uncompressed_len >> 24) & 0xFF);
    swf_data.insert(swf_data.end(), body_payload.begin(), body_payload.end());

    // 2. SWFParser Test
    SWFParser parser;
    bool parsed = parser.parse(swf_data.data(), swf_data.size());
    assert(parsed == true);
    assert(parser.get_header().background_color_xrgb == 0x00123456);
    printf("  [PASS] Tag 9 parsed stage background color 0x00123456 accurately.\n");

    // 3. Libretro Video Callback Test
    retro_init();
    retro_set_video_refresh(mock_video_refresh);

    struct retro_game_info game_info;
    memset(&game_info, 0, sizeof(game_info));
    game_info.data = swf_data.data();
    game_info.size = swf_data.size();

    bool loaded = retro_load_game(&game_info);
    assert(loaded == true);

    retro_run();

    assert(g_last_width == 640);
    assert(g_last_height == 480);
    assert(g_last_frame_color == 0x00123456);
    printf("  [PASS] retro_run successfully rendered 32-bit XRGB8888 frame matching Tag 9 background color.\n");

    retro_deinit();
    printf("[TEST] All Video Framebuffer tests passed successfully!\n");
    return 0;
}
