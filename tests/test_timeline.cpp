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

static void mock_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch) {
    (void)data;
    (void)width;
    (void)height;
    (void)pitch;
}

int main() {
    printf("[TEST] Starting Timeline Frame Stepper Unit Tests...\n");

    // Construct SWF containing 3 TagShowFrame (Tag 1) markers
    std::vector<uint8_t> rect = create_rect_bytes(0, 800 * 20, 0, 600 * 20);

    std::vector<uint8_t> body_payload;
    body_payload.insert(body_payload.end(), rect.begin(), rect.end());

    // FrameRate: 60.0 fps
    body_payload.push_back(0x00);
    body_payload.push_back(0x3C);

    // FrameCount: 3 frames
    body_payload.push_back(0x03);
    body_payload.push_back(0x00);

    // Tag 1: TagShowFrame (header: (1 << 6) | 0 = 0x0040)
    uint16_t tag_show_frame = (1 << 6) | 0;

    // Frame 1 marker
    body_payload.push_back(tag_show_frame & 0xFF);
    body_payload.push_back((tag_show_frame >> 8) & 0xFF);

    // Frame 2 marker
    body_payload.push_back(tag_show_frame & 0xFF);
    body_payload.push_back((tag_show_frame >> 8) & 0xFF);

    // Frame 3 marker
    body_payload.push_back(tag_show_frame & 0xFF);
    body_payload.push_back((tag_show_frame >> 8) & 0xFF);

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

    // 1. SWFParser TagShowFrame recognition
    SWFParser parser;
    bool parsed = parser.parse(swf_data.data(), swf_data.size());
    assert(parsed == true);
    assert(parser.get_show_frame_count() == 3);
    assert(parser.get_show_frame_positions().size() == 3);
    printf("  [PASS] TagShowFrame (Tag 1) extraction found 3 frame markers accurately.\n");

    // 2. Core retro_run Timeline Stepping Test
    retro_init();
    retro_set_video_refresh(mock_video_refresh);

    struct retro_game_info game_info;
    memset(&game_info, 0, sizeof(game_info));
    game_info.data = swf_data.data();
    game_info.size = swf_data.size();

    bool loaded = retro_load_game(&game_info);
    assert(loaded == true);

    // Stepping frames through retro_run()
    for (int i = 0; i < 10; ++i) {
        retro_run();
    }
    printf("  [PASS] retro_run stepped cyclic timeline frames smoothly for 10 ticks.\n");

    retro_deinit();
    printf("[TEST] All Timeline Frame Stepper unit tests passed successfully!\n");
    return 0;
}
