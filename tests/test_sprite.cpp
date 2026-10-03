#include "swf_parser.h"
#include <cassert>
#include <cstdio>
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

int main() {
    printf("[TEST] Starting MovieClip Sprite Unit Tests...\n");

    SWFParser parser;

    std::vector<uint8_t> rect = create_rect_bytes(0, 800 * 20, 0, 600 * 20);

    std::vector<uint8_t> body_payload;
    body_payload.insert(body_payload.end(), rect.begin(), rect.end());

    // FrameRate: 60.0 fps
    body_payload.push_back(0x00);
    body_payload.push_back(0x3C);

    // FrameCount: 1 frame
    body_payload.push_back(0x01);
    body_payload.push_back(0x00);

    // TagDefineSprite (Tag 39)
    // Inner payload: Sprite ID = 10, FrameCount = 5, followed by TagPlaceObject2 (depth 1, char 5) and TagEnd
    std::vector<uint8_t> sprite_payload;
    sprite_payload.push_back(0x0A); sprite_payload.push_back(0x00); // Sprite ID 10
    sprite_payload.push_back(0x05); sprite_payload.push_back(0x00); // Frame count 5

    // Nested TagPlaceObject2 inside sprite
    uint16_t place_hdr = (26 << 6) | 5;
    sprite_payload.push_back(place_hdr & 0xFF);
    sprite_payload.push_back((place_hdr >> 8) & 0xFF);
    sprite_payload.push_back(0x02); // Has character flag
    sprite_payload.push_back(0x01); sprite_payload.push_back(0x00); // Depth 1
    sprite_payload.push_back(0x05); sprite_payload.push_back(0x00); // Char ID 5

    // Nested TagEnd
    sprite_payload.push_back(0x00);
    sprite_payload.push_back(0x00);

    uint16_t sprite_hdr = (39 << 6) | static_cast<uint16_t>(sprite_payload.size());
    body_payload.push_back(sprite_hdr & 0xFF);
    body_payload.push_back((sprite_hdr >> 8) & 0xFF);
    body_payload.insert(body_payload.end(), sprite_payload.begin(), sprite_payload.end());

    // TagEnd
    body_payload.push_back(0x00);
    body_payload.push_back(0x00);

    uint32_t total_len = static_cast<uint32_t>(8 + body_payload.size());

    std::vector<uint8_t> swf_data = {'F', 'W', 'S', 15};
    swf_data.push_back(total_len & 0xFF);
    swf_data.push_back((total_len >> 8) & 0xFF);
    swf_data.push_back((total_len >> 16) & 0xFF);
    swf_data.push_back((total_len >> 24) & 0xFF);
    swf_data.insert(swf_data.end(), body_payload.begin(), body_payload.end());

    bool success = parser.parse(swf_data.data(), swf_data.size());
    assert(success == true);

    const DisplayList& dl = parser.get_display_list();
    const SWFSpriteDefinition* sprite = dl.find_sprite(10);
    assert(sprite != nullptr);
    assert(sprite->sprite_id == 10);
    assert(sprite->frame_count == 5);
    assert(sprite->sub_objects.size() == 1);
    assert(sprite->sub_objects[0].depth == 1);
    assert(sprite->sub_objects[0].character_id == 5);

    printf("  [PASS] TagDefineSprite parsed inner tags and sub-objects successfully.\n");

    printf("[TEST] All MovieClip Sprite unit tests passed successfully!\n");
    return 0;
}
