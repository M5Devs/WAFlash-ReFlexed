#include "swf_parser.h"
#include <zlib.h>
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
    uint8_t nbits = 15; // 15 bits per coordinate
    writer.write_bits(nbits, 5);
    writer.write_bits(static_cast<uint32_t>(xmin) & 0x7FFF, nbits);
    writer.write_bits(static_cast<uint32_t>(xmax) & 0x7FFF, nbits);
    writer.write_bits(static_cast<uint32_t>(ymin) & 0x7FFF, nbits);
    writer.write_bits(static_cast<uint32_t>(ymax) & 0x7FFF, nbits);
    writer.align_byte();
    return writer.get_bytes();
}

int main() {
    printf("[TEST] Starting SWF Header & Tag Parser Unit Tests...\n");

    // Construct uncompressed payload starting at byte 8 (RECT, FrameRate, FrameCount, Tags)
    std::vector<uint8_t> rect = create_rect_bytes(0, 800 * 20, 0, 600 * 20); // 800x600 px

    std::vector<uint8_t> body_payload;
    body_payload.insert(body_payload.end(), rect.begin(), rect.end());

    // FrameRate: 60.0 fps (8.8 fixed point: 0x00 frac, 0x3C int)
    body_payload.push_back(0x00);
    body_payload.push_back(0x3C);

    // FrameCount: 1 frame
    body_payload.push_back(0x01);
    body_payload.push_back(0x00);

    // TagDoABC2 (type 72): Header = (72 << 6) | length
    // Flags (4 bytes) + Name ("TestABC\0", 8 bytes) + Bytecode (4 bytes) = 16 bytes
    uint16_t tag_header = (72 << 6) | 16;
    body_payload.push_back(tag_header & 0xFF);
    body_payload.push_back((tag_header >> 8) & 0xFF);

    // Flags
    body_payload.push_back(0x01);
    body_payload.push_back(0x00);
    body_payload.push_back(0x00);
    body_payload.push_back(0x00);

    // Name "TestABC\0"
    const char* tag_name = "TestABC";
    for (size_t i = 0; i <= strlen(tag_name); ++i) {
        body_payload.push_back(static_cast<uint8_t>(tag_name[i]));
    }

    // Bytecode payload
    body_payload.push_back(0xAA);
    body_payload.push_back(0xBB);
    body_payload.push_back(0xCC);
    body_payload.push_back(0xDD);

    // TagEnd (type 0)
    body_payload.push_back(0x00);
    body_payload.push_back(0x00);

    uint32_t total_uncompressed_len = static_cast<uint32_t>(8 + body_payload.size());

    // --- TEST 1: Uncompressed FWS ---
    {
        std::vector<uint8_t> fws_data;
        fws_data.push_back('F');
        fws_data.push_back('W');
        fws_data.push_back('S');
        fws_data.push_back(15); // Version 15

        // FileLength (UI32)
        fws_data.push_back(total_uncompressed_len & 0xFF);
        fws_data.push_back((total_uncompressed_len >> 8) & 0xFF);
        fws_data.push_back((total_uncompressed_len >> 16) & 0xFF);
        fws_data.push_back((total_uncompressed_len >> 24) & 0xFF);

        fws_data.insert(fws_data.end(), body_payload.begin(), body_payload.end());

        SWFParser parser;
        bool result = parser.parse(fws_data.data(), fws_data.size());
        (void)result;
        assert(result == true);

        const SWFHeader& header = parser.get_header();
        (void)header;
        assert(header.signature[0] == 'F' && header.signature[1] == 'W' && header.signature[2] == 'S');
        assert(header.version == 15);
        assert(header.file_length == total_uncompressed_len);
        assert(header.width_px == 800);
        assert(header.height_px == 600);
        assert(header.frame_rate == 60.0f);
        assert(header.frame_count == 1);

        const auto& abc_tags = parser.get_abc_tags();
        (void)abc_tags;
        assert(abc_tags.size() == 1);
        assert(abc_tags[0].name == "TestABC");
        assert(abc_tags[0].flags == 1);
        assert(abc_tags[0].bytecode_data.size() == 4);
        assert(abc_tags[0].bytecode_data[0] == 0xAA);
        assert(abc_tags[0].bytecode_data[3] == 0xDD);

        printf("  [PASS] Test 1: Uncompressed FWS SWF header & TagDoABC2 parsed correctly.\n");
    }

    // --- TEST 2: Compressed CWS ---
    {
        std::vector<uint8_t> compressed_body(compressBound(body_payload.size()));
        uLongf compressed_size = compressed_body.size();
        int z_res = compress(compressed_body.data(), &compressed_size, body_payload.data(), body_payload.size());
        (void)z_res;
        assert(z_res == Z_OK);
        compressed_body.resize(compressed_size);

        std::vector<uint8_t> cws_data;
        cws_data.push_back('C');
        cws_data.push_back('W');
        cws_data.push_back('S');
        cws_data.push_back(15);

        // FileLength (uncompressed size)
        cws_data.push_back(total_uncompressed_len & 0xFF);
        cws_data.push_back((total_uncompressed_len >> 8) & 0xFF);
        cws_data.push_back((total_uncompressed_len >> 16) & 0xFF);
        cws_data.push_back((total_uncompressed_len >> 24) & 0xFF);

        cws_data.insert(cws_data.end(), compressed_body.begin(), compressed_body.end());

        SWFParser parser;
        bool result = parser.parse(cws_data.data(), cws_data.size());
        (void)result;
        assert(result == true);

        const SWFHeader& header = parser.get_header();
        (void)header;
        assert(header.signature[0] == 'C' && header.signature[1] == 'W' && header.signature[2] == 'S');
        assert(header.version == 15);
        assert(header.file_length == total_uncompressed_len);
        assert(header.width_px == 800);
        assert(header.height_px == 600);
        assert(header.frame_rate == 60.0f);
        assert(header.frame_count == 1);

        const auto& abc_tags = parser.get_abc_tags();
        (void)abc_tags;
        assert(abc_tags.size() == 1);
        assert(abc_tags[0].name == "TestABC");
        assert(abc_tags[0].flags == 1);
        assert(abc_tags[0].bytecode_data.size() == 4);

        printf("  [PASS] Test 2: Zlib-compressed CWS SWF decompressed and parsed correctly.\n");
    }

    printf("[TEST] All SWF Parser tests passed successfully!\n");
    return 0;
}
