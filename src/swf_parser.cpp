#include "swf_parser.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <zlib.h>

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : m_data(data), m_size(size), m_byte_offset(0), m_bit_offset(0) {}

    uint32_t read_bits(uint8_t count) {
        uint32_t result = 0;
        for (uint8_t i = 0; i < count; ++i) {
            if (m_byte_offset >= m_size) return result;
            uint8_t bit = (m_data[m_byte_offset] >> (7 - m_bit_offset)) & 0x01;
            result = (result << 1) | bit;
            m_bit_offset++;
            if (m_bit_offset == 8) {
                m_bit_offset = 0;
                m_byte_offset++;
            }
        }
        return result;
    }

    int32_t read_sbits(uint8_t count) {
        if (count == 0) return 0;
        uint32_t bits = read_bits(count);
        bool sign_bit = (bits >> (count - 1)) & 0x01;
        if (sign_bit) {
            uint32_t mask = (1U << count) - 1;
            return static_cast<int32_t>(bits | ~mask);
        }
        return static_cast<int32_t>(bits);
    }

    void align_byte() {
        if (m_bit_offset > 0) {
            m_bit_offset = 0;
            m_byte_offset++;
        }
    }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_byte_offset;
    uint8_t m_bit_offset;
};

SWFParser::SWFParser() : m_audio_mixer(nullptr) {
    m_header = {};
    m_sound_stream_header = {};
}

SWFParser::~SWFParser() = default;

bool SWFParser::parse(const uint8_t* data, size_t size) {
    if (!data || size < 8) return false;

    m_header.signature[0] = static_cast<char>(data[0]);
    m_header.signature[1] = static_cast<char>(data[1]);
    m_header.signature[2] = static_cast<char>(data[2]);
    m_header.version = data[3];

    m_header.file_length = static_cast<uint32_t>(data[4]) |
                          (static_cast<uint32_t>(data[5]) << 8) |
                          (static_cast<uint32_t>(data[6]) << 16) |
                          (static_cast<uint32_t>(data[7]) << 24);

    bool is_compressed = (m_header.signature[0] == 'C');
    if (!is_compressed && m_header.signature[0] != 'F') {
        return false;
    }

    std::vector<uint8_t> uncompressed_data;
    if (is_compressed) {
        z_stream strm{};
        strm.next_in = const_cast<Bytef*>(data + 8);
        strm.avail_in = static_cast<uInt>(size - 8);

        if (inflateInit(&strm) != Z_OK) return false;

        size_t expected_size = m_header.file_length > 8 ? (m_header.file_length - 8) : 1024;
        uncompressed_data.resize(expected_size);

        strm.next_out = reinterpret_cast<Bytef*>(uncompressed_data.data());
        strm.avail_out = static_cast<uInt>(uncompressed_data.size());

        int ret = inflate(&strm, Z_FINISH);
        if (ret != Z_STREAM_END && ret != Z_OK) {
            inflateEnd(&strm);
            return false;
        }

        uncompressed_data.resize(strm.total_out);
        inflateEnd(&strm);
    } else {
        uncompressed_data.assign(data + 8, data + size);
    }

    if (uncompressed_data.empty()) return false;

    // Parse Frame Size (RECT)
    BitReader reader(uncompressed_data.data(), uncompressed_data.size());
    uint8_t n_bits = static_cast<uint8_t>(reader.read_bits(5));
    int32_t xmin = reader.read_sbits(n_bits);
    int32_t xmax = reader.read_sbits(n_bits);
    int32_t ymin = reader.read_sbits(n_bits);
    int32_t ymax = reader.read_sbits(n_bits);

    m_header.width_px = static_cast<uint32_t>((xmax - xmin) / 20);
    m_header.height_px = static_cast<uint32_t>((ymax - ymin) / 20);

    reader.align_byte();

    // Parse FrameRate & FrameCount
    size_t offset = 0;
    // Calculate byte offset based on RECT length
    size_t rect_bit_len = 5 + 4 * n_bits;
    offset = (rect_bit_len + 7) / 8;

    if (offset + 4 > uncompressed_data.size()) return false;

    uint16_t fps_fixed = static_cast<uint16_t>(uncompressed_data[offset]) |
                         (static_cast<uint16_t>(uncompressed_data[offset + 1]) << 8);
    m_header.frame_rate = static_cast<float>(fps_fixed) / 256.0f;
    if (m_header.frame_rate <= 0.0f) {
        m_header.frame_rate = static_cast<float>(uncompressed_data[offset + 1]);
    }

    m_header.frame_count = static_cast<uint16_t>(uncompressed_data[offset + 2]) |
                          (static_cast<uint16_t>(uncompressed_data[offset + 3]) << 8);

    offset += 4;

    m_abc_tags.clear();
    m_show_frame_positions.clear();
    m_display_list.clear();
    m_stream_decoder.reset();
    m_sound_stream_header = {};

    auto parse_tag = [this, &uncompressed_data](uint16_t tag_type, size_t tag_offset, uint32_t tag_len, auto& self) -> void {
        if (tag_type == 1) { // TagShowFrame
            m_show_frame_positions.push_back(tag_offset);
        } else if (tag_type == 2 || tag_type == 22 || tag_type == 32) { // TagDefineShape 1, 2, 3
            if (tag_len >= 2) {
                uint16_t character_id = static_cast<uint16_t>(uncompressed_data[tag_offset]) |
                                       (static_cast<uint16_t>(uncompressed_data[tag_offset + 1]) << 8);

                BitReader shape_reader(uncompressed_data.data() + tag_offset + 2, tag_len - 2);
                uint8_t shape_nbits = static_cast<uint8_t>(shape_reader.read_bits(5));
                int32_t s_xmin = shape_reader.read_sbits(shape_nbits);
                int32_t s_xmax = shape_reader.read_sbits(shape_nbits);
                int32_t s_ymin = shape_reader.read_sbits(shape_nbits);
                int32_t s_ymax = shape_reader.read_sbits(shape_nbits);

                uint32_t fill_color = 0x00FFFFFF;

                // Parse shape records for fill styles if available
                shape_reader.align_byte();

                // Simple heuristic scan for solid color fill in TagDefineShape3 or TagDefineShape
                size_t payload_idx = tag_offset + 2 + ((5 + 4 * shape_nbits + 7) / 8);
                if (payload_idx + 1 < tag_offset + tag_len) {
                    uint8_t fill_style_count = uncompressed_data[payload_idx];
                    if (fill_style_count > 0 && fill_style_count < 0xFF) {
                        payload_idx++;
                        uint8_t fill_type = uncompressed_data[payload_idx];
                        if (fill_type == 0x00) { // Solid fill
                            payload_idx++;
                            if (tag_type == 32) { // RGBA
                                if (payload_idx + 4 <= tag_offset + tag_len) {
                                    uint8_t r = uncompressed_data[payload_idx];
                                    uint8_t g = uncompressed_data[payload_idx + 1];
                                    uint8_t b = uncompressed_data[payload_idx + 2];
                                    uint8_t a = uncompressed_data[payload_idx + 3];
                                    fill_color = (static_cast<uint32_t>(a) << 24) |
                                                 (static_cast<uint32_t>(r) << 16) |
                                                 (static_cast<uint32_t>(g) << 8) |
                                                 static_cast<uint32_t>(b);
                                }
                            } else {
                                if (payload_idx + 3 <= tag_offset + tag_len) {
                                    uint8_t r = uncompressed_data[payload_idx];
                                    uint8_t g = uncompressed_data[payload_idx + 1];
                                    uint8_t b = uncompressed_data[payload_idx + 2];
                                    fill_color = (static_cast<uint32_t>(r) << 16) |
                                                 (static_cast<uint32_t>(g) << 8) |
                                                 static_cast<uint32_t>(b);
                                }
                            }
                        }
                    }
                }

                SWFShapeDefinition shape;
                shape.character_id = character_id;
                shape.x_min = s_xmin / 20;
                shape.x_max = s_xmax / 20;
                shape.y_min = s_ymin / 20;
                shape.y_max = s_ymax / 20;
                shape.fill_color_xrgb = fill_color;

                m_display_list.register_shape(shape);
            }
        } else if (tag_type == 9) { // TagSetBackgroundColor
            if (tag_len >= 3) {
                uint8_t r = uncompressed_data[tag_offset];
                uint8_t g = uncompressed_data[tag_offset + 1];
                uint8_t b = uncompressed_data[tag_offset + 2];
                m_header.background_color_xrgb = (static_cast<uint32_t>(r) << 16) |
                                                 (static_cast<uint32_t>(g) << 8) |
                                                 static_cast<uint32_t>(b);
            }
        } else if (tag_type == 18 || tag_type == 45) { // TagSoundStreamHead (18) / TagSoundStreamHead2 (45)
            if (tag_len >= 4) {
                // Header format for Tag 45:
                // Byte 0: Mix/Playback format (Bits 0-1: rate, Bit 2: size, Bit 3: channels)
                // Byte 1: Stream format (Bits 0-1: rate, Bit 2: size, Bit 3: channels, Bits 4-7: format)
                // Bytes 2-3: SampleCount (UI16)
                uint8_t stream_flags = uncompressed_data[tag_offset + 1];
                uint8_t rate_code = (stream_flags >> 2) & 0x03;
                static const uint32_t rates[] = {5512, 11025, 22050, 44100};

                m_sound_stream_header.stream_format = (stream_flags >> 4) & 0x0F;
                m_sound_stream_header.sample_rate = rates[rate_code];
                m_sound_stream_header.is_16bit = ((stream_flags >> 1) & 0x01) != 0;
                m_sound_stream_header.is_stereo = (stream_flags & 0x01) != 0;
                m_sound_stream_header.sample_count_per_frame = static_cast<uint16_t>(uncompressed_data[tag_offset + 2]) |
                                                               (static_cast<uint16_t>(uncompressed_data[tag_offset + 3]) << 8);

                int16_t latency_seek = 0;
                if (m_sound_stream_header.stream_format == 2 && tag_len >= 6) { // MP3 format
                    latency_seek = static_cast<int16_t>(uncompressed_data[tag_offset + 4] | (uncompressed_data[tag_offset + 5] << 8));
                }
                m_sound_stream_header.latency_seek = latency_seek;
                m_sound_stream_header.is_active = true;

                SoundStreamHeader stream_hdr_dec{};
                stream_hdr_dec.format = static_cast<SoundFormat>(m_sound_stream_header.stream_format);
                stream_hdr_dec.sample_rate = m_sound_stream_header.sample_rate;
                stream_hdr_dec.is_16bit = m_sound_stream_header.is_16bit;
                stream_hdr_dec.is_stereo = m_sound_stream_header.is_stereo;
                stream_hdr_dec.samples_per_frame = m_sound_stream_header.sample_count_per_frame;
                stream_hdr_dec.latency_seek = m_sound_stream_header.latency_seek;
                stream_hdr_dec.active = true;

                m_stream_decoder.initialize(stream_hdr_dec);
            }
        } else if (tag_type == 19) { // TagSoundStreamBlock
            if (tag_len > 0) {
                const uint8_t* payload = uncompressed_data.data() + tag_offset;
                size_t payload_len = tag_len;

                auto decoded_samples = m_stream_decoder.decode_block(payload, payload_len);
                if (m_audio_mixer && !decoded_samples.empty()) {
                    m_audio_mixer->queue_samples(decoded_samples.data(), decoded_samples.size());
                }
            }
        } else if (tag_type == 26) { // TagPlaceObject2
            if (tag_len >= 3) {
                uint8_t flags = uncompressed_data[tag_offset];
                bool has_character = (flags & 0x02) != 0;
                bool has_matrix = (flags & 0x04) != 0;

                uint16_t depth = static_cast<uint16_t>(uncompressed_data[tag_offset + 1]) |
                                (static_cast<uint16_t>(uncompressed_data[tag_offset + 2]) << 8);

                size_t p_idx = tag_offset + 3;
                uint16_t character_id = 0;
                if (has_character && p_idx + 2 <= tag_offset + tag_len) {
                    character_id = static_cast<uint16_t>(uncompressed_data[p_idx]) |
                                  (static_cast<uint16_t>(uncompressed_data[p_idx + 1]) << 8);
                    p_idx += 2;
                }

                int32_t translate_x = 0;
                int32_t translate_y = 0;

                if (has_matrix && p_idx < tag_offset + tag_len) {
                    BitReader mat_reader(uncompressed_data.data() + p_idx, (tag_offset + tag_len) - p_idx);
                    bool has_scale = mat_reader.read_bits(1) != 0;
                    if (has_scale) {
                        uint8_t n_scale_bits = static_cast<uint8_t>(mat_reader.read_bits(5));
                        mat_reader.read_sbits(n_scale_bits);
                        mat_reader.read_sbits(n_scale_bits);
                    }
                    bool has_rotate = mat_reader.read_bits(1) != 0;
                    if (has_rotate) {
                        uint8_t n_rotate_bits = static_cast<uint8_t>(mat_reader.read_bits(5));
                        mat_reader.read_sbits(n_rotate_bits);
                        mat_reader.read_sbits(n_rotate_bits);
                    }
                    uint8_t n_trans_bits = static_cast<uint8_t>(mat_reader.read_bits(5));
                    translate_x = mat_reader.read_sbits(n_trans_bits);
                    translate_y = mat_reader.read_sbits(n_trans_bits);
                }

                int32_t trans_x_px = translate_x / 20;
                int32_t trans_y_px = translate_y / 20;

                m_display_list.place_object(depth, character_id, trans_x_px, trans_y_px);
            }
        } else if (tag_type == 28) { // TagRemoveObject2
            if (tag_len >= 2) {
                uint16_t depth = static_cast<uint16_t>(uncompressed_data[tag_offset]) |
                                (static_cast<uint16_t>(uncompressed_data[tag_offset + 1]) << 8);
                m_display_list.remove_object(depth);
            }
        } else if (tag_type == 39) { // TagDefineSprite
            if (tag_len >= 4) {
                uint16_t sprite_id = static_cast<uint16_t>(uncompressed_data[tag_offset]) |
                                    (static_cast<uint16_t>(uncompressed_data[tag_offset + 1]) << 8);
                uint16_t frame_count = static_cast<uint16_t>(uncompressed_data[tag_offset + 2]) |
                                      (static_cast<uint16_t>(uncompressed_data[tag_offset + 3]) << 8);

                SWFSpriteDefinition sprite;
                sprite.sprite_id = sprite_id;
                sprite.frame_count = frame_count;

                size_t sub_offset = tag_offset + 4;
                while (sub_offset + 2 <= tag_offset + tag_len) {
                    uint16_t sub_header = static_cast<uint16_t>(uncompressed_data[sub_offset]) |
                                         (static_cast<uint16_t>(uncompressed_data[sub_offset + 1]) << 8);
                    sub_offset += 2;

                    uint16_t sub_type = sub_header >> 6;
                    uint32_t sub_len = sub_header & 0x3F;

                    if (sub_len == 0x3F) {
                        if (sub_offset + 4 > tag_offset + tag_len) break;
                        sub_len = static_cast<uint32_t>(uncompressed_data[sub_offset]) |
                                 (static_cast<uint32_t>(uncompressed_data[sub_offset + 1]) << 8) |
                                 (static_cast<uint32_t>(uncompressed_data[sub_offset + 2]) << 16) |
                                 (static_cast<uint32_t>(uncompressed_data[sub_offset + 3]) << 24);
                        sub_offset += 4;
                    }

                    if (sub_offset + sub_len > tag_offset + tag_len) break;

                    if (sub_type == 26) { // TagPlaceObject2 inside sprite
                        if (sub_len >= 3) {
                            uint8_t flags = uncompressed_data[sub_offset];
                            bool has_char = (flags & 0x02) != 0;
                            bool has_mat = (flags & 0x04) != 0;
                            uint16_t depth = static_cast<uint16_t>(uncompressed_data[sub_offset + 1]) |
                                            (static_cast<uint16_t>(uncompressed_data[sub_offset + 2]) << 8);

                            size_t p_idx = sub_offset + 3;
                            uint16_t char_id = 0;
                            if (has_char && p_idx + 2 <= sub_offset + sub_len) {
                                char_id = static_cast<uint16_t>(uncompressed_data[p_idx]) |
                                         (static_cast<uint16_t>(uncompressed_data[p_idx + 1]) << 8);
                                p_idx += 2;
                            }

                            int32_t tx = 0, ty = 0;
                            if (has_mat && p_idx < sub_offset + sub_len) {
                                BitReader mat_reader(uncompressed_data.data() + p_idx, (sub_offset + sub_len) - p_idx);
                                if (mat_reader.read_bits(1) != 0) {
                                    uint8_t n_bits = static_cast<uint8_t>(mat_reader.read_bits(5));
                                    mat_reader.read_sbits(n_bits);
                                    mat_reader.read_sbits(n_bits);
                                }
                                if (mat_reader.read_bits(1) != 0) {
                                    uint8_t n_bits = static_cast<uint8_t>(mat_reader.read_bits(5));
                                    mat_reader.read_sbits(n_bits);
                                    mat_reader.read_sbits(n_bits);
                                }
                                uint8_t n_trans_bits = static_cast<uint8_t>(mat_reader.read_bits(5));
                                tx = mat_reader.read_sbits(n_trans_bits);
                                ty = mat_reader.read_sbits(n_trans_bits);
                            }

                            DisplayObject sub_obj;
                            sub_obj.depth = depth;
                            sub_obj.character_id = char_id;
                            sub_obj.transform_x = tx / 20;
                            sub_obj.transform_y = ty / 20;
                            sprite.sub_objects.push_back(sub_obj);
                        }
                    }

                    // Recursively process other nested tags in sprite
                    self(sub_type, sub_offset, sub_len, self);

                    sub_offset += sub_len;
                    if (sub_type == 0) break; // TagEnd
                }

                m_display_list.register_sprite(sprite);
            }
        } else if (tag_type == 72 || tag_type == 82) { // TagDoABC2 / TagDoABC
            if (tag_len >= 4) {
                uint32_t flags = static_cast<uint32_t>(uncompressed_data[tag_offset]) |
                                (static_cast<uint32_t>(uncompressed_data[tag_offset + 1]) << 8) |
                                (static_cast<uint32_t>(uncompressed_data[tag_offset + 2]) << 16) |
                                (static_cast<uint32_t>(uncompressed_data[tag_offset + 3]) << 24);

                size_t tag_sub_offset = 4;
                std::string name;
                while (tag_sub_offset < tag_len && uncompressed_data[tag_offset + tag_sub_offset] != 0) {
                    name += static_cast<char>(uncompressed_data[tag_offset + tag_sub_offset]);
                    tag_sub_offset++;
                }
                if (tag_sub_offset < tag_len && uncompressed_data[tag_offset + tag_sub_offset] == 0) {
                    tag_sub_offset++;
                }

                std::vector<uint8_t> bytecode_data(
                    uncompressed_data.begin() + tag_offset + tag_sub_offset,
                    uncompressed_data.begin() + tag_offset + tag_len
                );

                m_abc_tags.push_back({flags, name, bytecode_data});
            }
        }
    };

    // Scan tags loop
    while (offset + 2 <= uncompressed_data.size()) {
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

        parse_tag(tag_type, offset, tag_length, parse_tag);

        offset += tag_length;

        if (tag_type == 0) { // TagEnd
            break;
        }
    }

    return true;
}
