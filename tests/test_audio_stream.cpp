#include "swf_parser.h"
#include "audio_stream_decoder.h"
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

void test_mp3_header_and_decoder_standalone() {
    printf("  [TEST 1] Testing AudioStreamDecoder MP3 initialization & latency seek parsing...\n");

    SoundStreamHeader header{};
    header.format = SoundFormat::MP3;
    header.sample_rate = 44100;
    header.is_16bit = true;
    header.is_stereo = true;
    header.samples_per_frame = 1152;
    header.latency_seek = 576;
    header.active = true;

    AudioStreamDecoder decoder;
    decoder.initialize(header);

    assert(decoder.get_header().format == SoundFormat::MP3);
    assert(decoder.get_header().sample_rate == 44100);
    assert(decoder.get_header().latency_seek == 576);

    // Decoding empty/invalid buffer handles gracefully without crashing
    uint8_t dummy_data[8] = {0x00, 0x00, 0xFF, 0xFB, 0x90, 0x64, 0x00, 0x00};
    auto samples = decoder.decode_block(dummy_data, sizeof(dummy_data));
    (void)samples;

    printf("  [PASS] AudioStreamDecoder standalone test completed.\n");
}

void test_swf_tag_45_parsing() {
    printf("  [TEST 2] Testing Tag 45 (SoundStreamHead2) and Tag 19 (SoundStreamBlock) parsing...\n");

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

    // TagSoundStreamHead2 (Tag 45, len=6)
    // Tag header: (45 << 6) | 6
    uint16_t tag_head_hdr = (45 << 6) | 6;
    body_payload.push_back(tag_head_hdr & 0xFF);
    body_payload.push_back((tag_head_hdr >> 8) & 0xFF);

    body_payload.push_back(0x00); // Mix format
    // Stream format byte: format=2 (MP3), rate=3 (44kHz), 16bit=1, stereo=1 -> (2<<4)|(3<<2)|(1<<1)|1 = 0x2F
    body_payload.push_back(0x2F);
    body_payload.push_back(0x80); // SampleCount: 1152 samples
    body_payload.push_back(0x04);
    body_payload.push_back(0x40); // LatencySeek: 576 samples
    body_payload.push_back(0x02);

    // TagSoundStreamBlock (Tag 19, len=8)
    uint16_t tag_block_hdr = (19 << 6) | 8;
    body_payload.push_back(tag_block_hdr & 0xFF);
    body_payload.push_back((tag_block_hdr >> 8) & 0xFF);

    // 2-byte MP3 sample seek + 6 bytes dummy bitstream
    body_payload.push_back(0x00); body_payload.push_back(0x00);
    body_payload.push_back(0xFF); body_payload.push_back(0xFB);
    body_payload.push_back(0x90); body_payload.push_back(0x64);
    body_payload.push_back(0x00); body_payload.push_back(0x00);

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
    assert(stream_hdr.stream_format == 2); // MP3
    assert(stream_hdr.sample_rate == 44100);
    assert(stream_hdr.is_16bit == true);
    assert(stream_hdr.is_stereo == true);
    assert(stream_hdr.latency_seek == 576);

    printf("  [PASS] Tag 45 MP3 header parsing and LatencySeek extraction validated.\n");
}

int main() {
    printf("[TEST] Starting Phase A Audio Stream Decoder Unit Tests...\n");
    test_mp3_header_and_decoder_standalone();
    test_swf_tag_45_parsing();
    printf("[TEST] All Phase A Audio Stream Decoder unit tests passed successfully!\n");
    return 0;
}
