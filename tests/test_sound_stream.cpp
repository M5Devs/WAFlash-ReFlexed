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
    printf("[TEST] Starting Sound Stream Unit Tests...\n");

    AudioMixer mixer;
    SWFParser parser;
    parser.set_audio_mixer(&mixer);

    std::vector<uint8_t> rect = create_rect_bytes(0, 800 * 20, 0, 600 * 20);

    std::vector<uint8_t> body_payload;
    body_payload.insert(body_payload.end(), rect.begin(), rect.end());

    // FrameRate: 60.0 fps
    body_payload.push_back(0x00);
    body_payload.push_back(0x3C);

    // FrameCount: 1 frame
    body_payload.push_back(0x01);
    body_payload.push_back(0x00);

    // TagSoundStreamHead2 (Tag 45, len=4)
    uint16_t tag_head_hdr = (45 << 6) | 4;
    body_payload.push_back(tag_head_hdr & 0xFF);
    body_payload.push_back((tag_head_hdr >> 8) & 0xFF);

    body_payload.push_back(0x00); // Mix format
    // Stream format byte: format=0 (PCM), rate=3 (44kHz), 16bit=1, stereo=1 -> (0<<4)|(3<<2)|(1<<1)|1 = 0x0F
    body_payload.push_back(0x0F);
    body_payload.push_back(0x64); // 100 samples
    body_payload.push_back(0x00);

    // TagSoundStreamBlock (Tag 19, len=16) -> 4 stereo 16-bit PCM samples
    uint16_t tag_block_hdr = (19 << 6) | 16;
    body_payload.push_back(tag_block_hdr & 0xFF);
    body_payload.push_back((tag_block_hdr >> 8) & 0xFF);

    // Sample 1: L=1000, R=1000
    body_payload.push_back(0xE8); body_payload.push_back(0x03);
    body_payload.push_back(0xE8); body_payload.push_back(0x03);
    // Sample 2: L=2000, R=2000
    body_payload.push_back(0xD0); body_payload.push_back(0x07);
    body_payload.push_back(0xD0); body_payload.push_back(0x07);
    // Sample 3: L=-1000, R=-1000
    body_payload.push_back(0x18); body_payload.push_back(0xFC);
    body_payload.push_back(0x18); body_payload.push_back(0xFC);
    // Sample 4: L=-2000, R=-2000
    body_payload.push_back(0x30); body_payload.push_back(0xF8);
    body_payload.push_back(0x30); body_payload.push_back(0xF8);

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

    const SWFSoundStreamHeader& stream_hdr = parser.get_sound_stream_header();
    assert(stream_hdr.is_active == true);
    assert(stream_hdr.sample_rate == 44100);
    assert(stream_hdr.is_16bit == true);
    assert(stream_hdr.is_stereo == true);

    assert(mixer.get_queued_frames() == 0); // Not decoded immediately during parse
    parser.apply_frame(0);
    assert(mixer.get_queued_frames() == 4); // Decoded after applying frame 0
    printf("  [PASS] Tag 45 and Tag 19 sound block parsed into frame bucket and decoded via apply_frame(0) successfully.\n");

    printf("[TEST] All Sound Stream unit tests passed successfully!\n");
    return 0;
}
