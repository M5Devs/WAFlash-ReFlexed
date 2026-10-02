#include "swf_parser.h"
#include <zlib.h>
#include <cstring>
#include <algorithm>
#include <iostream>

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size)
        : m_data(data), m_size(size), m_byte_idx(0), m_bit_idx(0) {}

    uint32_t read_bits(uint8_t n) {
        uint32_t val = 0;
        for (uint8_t i = 0; i < n; ++i) {
            if (m_byte_idx >= m_size) return 0;
            uint8_t bit = (m_data[m_byte_idx] >> (7 - m_bit_idx)) & 1;
            val = (val << 1) | bit;
            m_bit_idx++;
            if (m_bit_idx == 8) {
                m_bit_idx = 0;
                m_byte_idx++;
            }
        }
        return val;
    }

    int32_t read_sbits(uint8_t n) {
        if (n == 0) return 0;
        uint32_t val = read_bits(n);
        if (val & (1U << (n - 1))) {
            val |= (~0U << n);
        }
        return static_cast<int32_t>(val);
    }

    void align_byte() {
        if (m_bit_idx != 0) {
            m_bit_idx = 0;
            m_byte_idx++;
        }
    }

    size_t get_byte_offset() const {
        return m_byte_idx + (m_bit_idx > 0 ? 1 : 0);
    }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_byte_idx;
    uint8_t m_bit_idx;
};

SWFParser::SWFParser() {
    std::memset(&m_header, 0, sizeof(SWFHeader));
    m_header.background_color_xrgb = 0x00FFFFFF;
}

SWFParser::~SWFParser() {}

bool SWFParser::parse(const uint8_t* data, size_t size) {
    if (!data || size < 8) {
        return false;
    }

    m_abc_tags.clear();
    m_show_frame_positions.clear();
    std::memset(&m_header, 0, sizeof(SWFHeader));
    m_header.background_color_xrgb = 0x00FFFFFF;

    m_header.signature[0] = static_cast<char>(data[0]);
    m_header.signature[1] = static_cast<char>(data[1]);
    m_header.signature[2] = static_cast<char>(data[2]);
    m_header.version = data[3];

    m_header.file_length = static_cast<uint32_t>(data[4]) |
                          (static_cast<uint32_t>(data[5]) << 8) |
                          (static_cast<uint32_t>(data[6]) << 16) |
                          (static_cast<uint32_t>(data[7]) << 24);

    bool is_fws = (m_header.signature[0] == 'F' && m_header.signature[1] == 'W' && m_header.signature[2] == 'S');
    bool is_cws = (m_header.signature[0] == 'C' && m_header.signature[1] == 'W' && m_header.signature[2] == 'S');

    if (!is_fws && !is_cws) {
        return false;
    }

    std::vector<uint8_t> uncompressed_data;

    if (is_cws) {
        if (m_header.file_length < 8) {
            return false;
        }
        uncompressed_data.resize(m_header.file_length);
        std::memcpy(uncompressed_data.data(), data, 8);

        uLongf dest_len = static_cast<uLongf>(m_header.file_length - 8);
        int res = uncompress(uncompressed_data.data() + 8, &dest_len, data + 8, static_cast<uLong>(size - 8));
        if (res != Z_OK) {
            return false;
        }
    } else {
        uncompressed_data.assign(data, data + size);
    }

    if (uncompressed_data.size() < 12) {
        return false;
    }

    // Parse RECT
    BitReader reader(uncompressed_data.data() + 8, uncompressed_data.size() - 8);
    uint8_t nbits = static_cast<uint8_t>(reader.read_bits(5));
    int32_t x_min = reader.read_sbits(nbits);
    int32_t x_max = reader.read_sbits(nbits);
    int32_t y_min = reader.read_sbits(nbits);
    int32_t y_max = reader.read_sbits(nbits);
    reader.align_byte();

    int32_t width_twips = x_max - x_min;
    int32_t height_twips = y_max - y_min;
    m_header.width_px = static_cast<uint32_t>(width_twips > 0 ? width_twips / 20 : 0);
    m_header.height_px = static_cast<uint32_t>(height_twips > 0 ? height_twips / 20 : 0);

    size_t offset = 8 + reader.get_byte_offset();
    if (offset + 4 > uncompressed_data.size()) {
        return false;
    }

    // FrameRate (UI16 fixed 8.8)
    uint8_t fps_frac = uncompressed_data[offset];
    uint8_t fps_int = uncompressed_data[offset + 1];
    m_header.frame_rate = static_cast<float>(fps_int) + (static_cast<float>(fps_frac) / 256.0f);
    offset += 2;

    // FrameCount (UI16)
    m_header.frame_count = static_cast<uint16_t>(uncompressed_data[offset]) |
                          (static_cast<uint16_t>(uncompressed_data[offset + 1]) << 8);
    offset += 2;

    // Scan tags
    while (offset + 2 <= uncompressed_data.size()) {
        size_t tag_start_offset = offset;
        uint16_t tag_code_and_length = static_cast<uint16_t>(uncompressed_data[offset]) |
                                       (static_cast<uint16_t>(uncompressed_data[offset + 1]) << 8);
        offset += 2;

        uint16_t tag_type = tag_code_and_length >> 6;
        uint32_t tag_length = tag_code_and_length & 0x3F;

        if (tag_length == 0x3F) {
            if (offset + 4 > uncompressed_data.size()) break;
            tag_length = static_cast<uint32_t>(uncompressed_data[offset]) |
                        (static_cast<uint32_t>(uncompressed_data[offset + 1]) << 8) |
                        (static_cast<uint32_t>(uncompressed_data[offset + 2]) << 16) |
                        (static_cast<uint32_t>(uncompressed_data[offset + 3]) << 24);
            offset += 4;
        }

        if (offset + tag_length > uncompressed_data.size()) {
            break;
        }

        // TagShowFrame (1)
        if (tag_type == 1) {
            m_show_frame_positions.push_back(tag_start_offset);
        }

        // TagSetBackgroundColor (9)
        if (tag_type == 9) {
            if (tag_length >= 3) {
                uint8_t r = uncompressed_data[offset];
                uint8_t g = uncompressed_data[offset + 1];
                uint8_t b = uncompressed_data[offset + 2];
                m_header.background_color_xrgb = (static_cast<uint32_t>(r) << 16) |
                                                 (static_cast<uint32_t>(g) << 8) |
                                                 static_cast<uint32_t>(b);
            }
        }

        // TagDoABC2 (72) or TagDoABC (82)
        if (tag_type == 72 || tag_type == 82) {
            if (tag_length >= 4) {
                uint32_t flags = static_cast<uint32_t>(uncompressed_data[offset]) |
                                (static_cast<uint32_t>(uncompressed_data[offset + 1]) << 8) |
                                (static_cast<uint32_t>(uncompressed_data[offset + 2]) << 16) |
                                (static_cast<uint32_t>(uncompressed_data[offset + 3]) << 24);

                size_t tag_offset = 4;
                std::string name;
                while (tag_offset < tag_length && uncompressed_data[offset + tag_offset] != 0) {
                    name += static_cast<char>(uncompressed_data[offset + tag_offset]);
                    tag_offset++;
                }
                if (tag_offset < tag_length && uncompressed_data[offset + tag_offset] == 0) {
                    tag_offset++; // Skip null terminator
                }

                std::vector<uint8_t> bytecode_data(
                    uncompressed_data.begin() + offset + tag_offset,
                    uncompressed_data.begin() + offset + tag_length
                );

                m_abc_tags.push_back({flags, name, bytecode_data});
            }
        }

        offset += tag_length;

        if (tag_type == 0) {
            // TagEnd
            break;
        }
    }

    return true;
}
