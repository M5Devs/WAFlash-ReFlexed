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

SWFParser::SWFParser() : m_audio_mixer(nullptr) {
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
    m_display_list.clear();
    m_sound_stream_header = SWFSoundStreamHeader();
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

    // Helper lambda for parsing tags recursively (for sprites)
    auto parse_tag = [&](uint16_t tag_type, size_t tag_offset, uint32_t tag_len, auto& self) -> void {
        if (tag_type == 1) { // TagShowFrame
            m_show_frame_positions.push_back(tag_offset - 2); // approximate offset
        } else if (tag_type == 2 || tag_type == 22 || tag_type == 32) { // TagDefineShape
            if (tag_len >= 2) {
                uint16_t character_id = static_cast<uint16_t>(uncompressed_data[tag_offset]) |
                                       (static_cast<uint16_t>(uncompressed_data[tag_offset + 1]) << 8);

                BitReader tag_reader(uncompressed_data.data() + tag_offset + 2, tag_len - 2);
                uint8_t shape_nbits = static_cast<uint8_t>(tag_reader.read_bits(5));
                int32_t s_xmin = tag_reader.read_sbits(shape_nbits);
                int32_t s_xmax = tag_reader.read_sbits(shape_nbits);
                int32_t s_ymin = tag_reader.read_sbits(shape_nbits);
                int32_t s_ymax = tag_reader.read_sbits(shape_nbits);
                tag_reader.align_byte();

                uint32_t fill_color = 0x00FFFFFF;
                size_t payload_idx = tag_offset + 2 + tag_reader.get_byte_offset();

                if (payload_idx < tag_offset + tag_len) {
                    uint8_t fill_style_count = uncompressed_data[payload_idx++];
                    if (fill_style_count == 0xFF && payload_idx + 1 < tag_offset + tag_len) {
                        fill_style_count = uncompressed_data[payload_idx] | (uncompressed_data[payload_idx + 1] << 8);
                        payload_idx += 2;
                    }

                    if (fill_style_count > 0 && payload_idx < tag_offset + tag_len) {
                        uint8_t fill_style_type = uncompressed_data[payload_idx++];
                        if (fill_style_type == 0x00) {
                            if (tag_type == 32) {
                                if (payload_idx + 4 <= tag_offset + tag_len) {
                                    uint8_t r = uncompressed_data[payload_idx];
                                    uint8_t g = uncompressed_data[payload_idx + 1];
                                    uint8_t b = uncompressed_data[payload_idx + 2];
                                    fill_color = (static_cast<uint32_t>(r) << 16) |
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
                m_sound_stream_header.is_active = true;
            }
        } else if (tag_type == 19) { // TagSoundStreamBlock
            if (tag_len > 0) {
                const uint8_t* payload = uncompressed_data.data() + tag_offset;
                size_t payload_len = tag_len;

                // For MP3 stream format (format 2), skip first 4 bytes sample count header if present
                if (m_sound_stream_header.stream_format == 2 && payload_len >= 4) {
                    payload += 4;
                    payload_len -= 4;
                }

                if (m_audio_mixer && payload_len > 0) {
                    // For uncompressed LE PCM (format 0 or 3), extract samples and convert to 44.1kHz stereo
                    if (m_sound_stream_header.stream_format == 0 || m_sound_stream_header.stream_format == 3) {
                        size_t sample_size_bytes = m_sound_stream_header.is_16bit ? 2 : 1;
                        size_t channels = m_sound_stream_header.is_stereo ? 2 : 1;
                        size_t frame_bytes = sample_size_bytes * channels;
                        size_t total_input_frames = payload_len / frame_bytes;

                        std::vector<int16_t> pcm_samples;
                        pcm_samples.reserve(total_input_frames * 2); // stereo

                        for (size_t f = 0; f < total_input_frames; ++f) {
                            int16_t left = 0, right = 0;
                            size_t frame_offset = f * frame_bytes;

                            if (m_sound_stream_header.is_16bit) {
                                if (m_sound_stream_header.stream_format == 0) { // LE
                                    left = static_cast<int16_t>(payload[frame_offset] | (payload[frame_offset + 1] << 8));
                                    if (channels == 2) {
                                        right = static_cast<int16_t>(payload[frame_offset + 2] | (payload[frame_offset + 3] << 8));
                                    } else {
                                        right = left;
                                    }
                                } else { // BE
                                    left = static_cast<int16_t>((payload[frame_offset] << 8) | payload[frame_offset + 1]);
                                    if (channels == 2) {
                                        right = static_cast<int16_t>((payload[frame_offset + 2] << 8) | payload[frame_offset + 3]);
                                    } else {
                                        right = left;
                                    }
                                }
                            } else { // 8-bit unsigned -> int16
                                left = static_cast<int16_t>((static_cast<int32_t>(payload[frame_offset]) - 128) * 256);
                                if (channels == 2) {
                                    right = static_cast<int16_t>((static_cast<int32_t>(payload[frame_offset + 1]) - 128) * 256);
                                } else {
                                    right = left;
                                }
                            }

                            // Resample from m_sound_stream_header.sample_rate to 44100Hz if necessary
                            uint32_t src_rate = m_sound_stream_header.sample_rate;
                            if (src_rate > 0 && src_rate != 44100) {
                                float ratio = 44100.0f / static_cast<float>(src_rate);
                                size_t target_samples = static_cast<size_t>(ratio);
                                for (size_t r = 0; r < target_samples; ++r) {
                                    pcm_samples.push_back(left);
                                    pcm_samples.push_back(right);
                                }
                            } else {
                                pcm_samples.push_back(left);
                                pcm_samples.push_back(right);
                            }
                        }

                        if (!pcm_samples.empty()) {
                            m_audio_mixer->queue_samples(pcm_samples.data(), pcm_samples.size());
                        }
                    } else {
                        // ADPCM/MP3 placeholder: Generate subtle synthetic tone for audio testing
                        m_audio_mixer->generate_tone(ToneType::Sine, 440.0f, 0.05f, 0.2f);
                    }
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
