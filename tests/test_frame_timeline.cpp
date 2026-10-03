#include "swf_parser.h"
#include "audio_mixer.h"
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
    printf("[TEST] Starting Per-Frame Timeline Command Queue Unit Tests...\n");

    AudioMixer mixer;
    SWFParser parser;
    parser.set_audio_mixer(&mixer);

    std::vector<uint8_t> rect = create_rect_bytes(0, 800 * 20, 0, 600 * 20);

    std::vector<uint8_t> body_payload;
    body_payload.insert(body_payload.end(), rect.begin(), rect.end());

    // FrameRate: 60.0 fps
    body_payload.push_back(0x00);
    body_payload.push_back(0x3C);

    // FrameCount: 3 frames
    body_payload.push_back(0x03);
    body_payload.push_back(0x00);

    // Frame 0 setup:
    // TagSoundStreamHead2 (Tag 45, len=4)
    uint16_t tag_head_hdr = (45 << 6) | 4;
    body_payload.push_back(tag_head_hdr & 0xFF);
    body_payload.push_back((tag_head_hdr >> 8) & 0xFF);
    body_payload.push_back(0x00);
    body_payload.push_back(0x0F); // PCM, 44100Hz, 16bit, Stereo
    body_payload.push_back(0x64);
    body_payload.push_back(0x00);

    // Frame 0: TagPlaceObject2 (Tag 26, len=5) -> Place character 1 at depth 1
    // Header: (26 << 6) | 5 = 0x0685
    uint16_t tag_place1_hdr = (26 << 6) | 5;
    body_payload.push_back(tag_place1_hdr & 0xFF);
    body_payload.push_back((tag_place1_hdr >> 8) & 0xFF);
    body_payload.push_back(0x02); // has_character=true
    body_payload.push_back(0x01); body_payload.push_back(0x00); // depth = 1
    body_payload.push_back(0x01); body_payload.push_back(0x00); // character_id = 1

    // TagSoundStreamBlock (Tag 19, len=8) -> 2 stereo 16-bit PCM samples
    uint16_t tag_block1_hdr = (19 << 6) | 8;
    body_payload.push_back(tag_block1_hdr & 0xFF);
    body_payload.push_back((tag_block1_hdr >> 8) & 0xFF);
    body_payload.push_back(0x00); body_payload.push_back(0x01);
    body_payload.push_back(0x00); body_payload.push_back(0x01);
    body_payload.push_back(0x00); body_payload.push_back(0x02);
    body_payload.push_back(0x00); body_payload.push_back(0x02);

    // TagShowFrame (Tag 1, len=0)
    uint16_t tag_show_frame = (1 << 6) | 0;
    body_payload.push_back(tag_show_frame & 0xFF);
    body_payload.push_back((tag_show_frame >> 8) & 0xFF);

    // Frame 1 setup:
    // TagPlaceObject2 -> Place character 2 at depth 2
    uint16_t tag_place2_hdr = (26 << 6) | 5;
    body_payload.push_back(tag_place2_hdr & 0xFF);
    body_payload.push_back((tag_place2_hdr >> 8) & 0xFF);
    body_payload.push_back(0x02);
    body_payload.push_back(0x02); body_payload.push_back(0x00); // depth = 2
    body_payload.push_back(0x02); body_payload.push_back(0x00); // character_id = 2

    // TagShowFrame
    body_payload.push_back(tag_show_frame & 0xFF);
    body_payload.push_back((tag_show_frame >> 8) & 0xFF);

    // Frame 2 setup:
    // TagRemoveObject2 (Tag 28, len=2) -> Remove depth 1
    uint16_t tag_remove_hdr = (28 << 6) | 2;
    body_payload.push_back(tag_remove_hdr & 0xFF);
    body_payload.push_back((tag_remove_hdr >> 8) & 0xFF);
    body_payload.push_back(0x01); body_payload.push_back(0x00); // depth = 1

    // TagShowFrame
    body_payload.push_back(tag_show_frame & 0xFF);
    body_payload.push_back((tag_show_frame >> 8) & 0xFF);

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

    bool parsed = parser.parse(swf_data.data(), swf_data.size());
    assert(parsed == true);

    const auto& frames = parser.get_timeline_frames();
    assert(frames.size() == 3);

    // Verify segregation of commands into frames
    assert(frames[0].place_commands.size() == 1);
    assert(frames[0].place_commands[0].depth == 1);
    assert(frames[0].place_commands[0].character_id == 1);
    assert(frames[0].remove_commands.empty());
    assert(frames[0].sound_stream_block.size() == 8);

    assert(frames[1].place_commands.size() == 1);
    assert(frames[1].place_commands[0].depth == 2);
    assert(frames[1].place_commands[0].character_id == 2);
    assert(frames[1].remove_commands.empty());
    assert(frames[1].sound_stream_block.empty());

    assert(frames[2].place_commands.empty());
    assert(frames[2].remove_commands.size() == 1);
    assert(frames[2].remove_commands[0].depth == 1);
    assert(frames[2].sound_stream_block.empty());

    printf("  [PASS] Commands (place, remove, sound block) correctly segregated into 3 frames.\n");

    // Test incremental execution via apply_frame
    // Before applying frame 0: DisplayList is empty, mixer has 0 frames
    assert(parser.get_display_list().get_active_objects().empty());
    assert(mixer.get_queued_frames() == 0);

    // Apply Frame 0
    parser.apply_frame(0);
    assert(parser.get_display_list().get_active_objects().size() == 1);
    assert(parser.get_display_list().get_active_objects().count(1) == 1);
    assert(mixer.get_queued_frames() == 2);

    // Apply Frame 1
    parser.apply_frame(1);
    assert(parser.get_display_list().get_active_objects().size() == 2);
    assert(parser.get_display_list().get_active_objects().count(1) == 1);
    assert(parser.get_display_list().get_active_objects().count(2) == 1);

    // Apply Frame 2
    parser.apply_frame(2);
    assert(parser.get_display_list().get_active_objects().size() == 1);
    assert(parser.get_display_list().get_active_objects().count(1) == 0);
    assert(parser.get_display_list().get_active_objects().count(2) == 1);

    printf("  [PASS] Frame-by-frame timeline execution mutated DisplayList and AudioMixer incrementally.\n");

    printf("[TEST] All Per-Frame Timeline Command Queue unit tests passed successfully!\n");
    return 0;
}
