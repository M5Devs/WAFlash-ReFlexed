#include "swf_parser.h"
#include "vector_rasterizer.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <zlib.h>

Matrix2D read_swf_matrix(BitReader& mat_reader) {
    Matrix2D mat;
    if (mat_reader.is_eof()) return mat;
    bool has_scale = mat_reader.read_bits(1) != 0;
    if (has_scale) {
        uint8_t n_scale_bits = static_cast<uint8_t>(mat_reader.read_bits(5));
        int32_t scale_x = mat_reader.read_sbits(n_scale_bits);
        int32_t scale_y = mat_reader.read_sbits(n_scale_bits);
        mat.a = static_cast<float>(scale_x) / 65536.0f;
        mat.d = static_cast<float>(scale_y) / 65536.0f;
    }
    bool has_rotate = mat_reader.read_bits(1) != 0;
    if (has_rotate) {
        uint8_t n_rotate_bits = static_cast<uint8_t>(mat_reader.read_bits(5));
        int32_t skew_0 = mat_reader.read_sbits(n_rotate_bits);
        int32_t skew_1 = mat_reader.read_sbits(n_rotate_bits);
        mat.b = static_cast<float>(skew_0) / 65536.0f;
        mat.c = static_cast<float>(skew_1) / 65536.0f;
    }
    uint8_t n_trans_bits = static_cast<uint8_t>(mat_reader.read_bits(5));
    int32_t translate_x = mat_reader.read_sbits(n_trans_bits);
    int32_t translate_y = mat_reader.read_sbits(n_trans_bits);
    mat.tx = static_cast<float>(translate_x) / 20.0f;
    mat.ty = static_cast<float>(translate_y) / 20.0f;
    return mat;
}

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
    m_timeline_frames.clear();
    m_current_frame_builder = SWFFrame{};
    m_display_list.clear();
    m_stream_decoder.reset();
    m_sound_stream_header = {};

    auto parse_tag = [this, &uncompressed_data](uint16_t tag_type, size_t tag_offset, uint32_t tag_len, auto& self) -> void {
        if (tag_type == 1) { // TagShowFrame
            m_show_frame_positions.push_back(tag_offset);
            m_timeline_frames.push_back(std::move(m_current_frame_builder));
            m_current_frame_builder = SWFFrame{};
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

                shape_reader.align_byte();

                auto parse_fill_style_array = [&shape_reader, tag_type, &fill_color](auto& self_fill) -> void {
                    (void)self_fill;
                    if (shape_reader.is_eof()) return;
                    uint32_t fill_count = shape_reader.read_bits(8);
                    if (fill_count == 0xFF) {
                        fill_count = shape_reader.read_bits(16);
                    }
                    for (uint32_t i = 0; i < fill_count && !shape_reader.is_eof(); ++i) {
                        uint8_t fill_type = static_cast<uint8_t>(shape_reader.read_bits(8));
                        if (fill_type == 0x00) { // Solid fill
                            if (tag_type == 32) { // RGBA
                                uint8_t r = static_cast<uint8_t>(shape_reader.read_bits(8));
                                uint8_t g = static_cast<uint8_t>(shape_reader.read_bits(8));
                                uint8_t b = static_cast<uint8_t>(shape_reader.read_bits(8));
                                uint8_t a = static_cast<uint8_t>(shape_reader.read_bits(8));
                                if (i == 0) {
                                    fill_color = (static_cast<uint32_t>(a) << 24) |
                                                 (static_cast<uint32_t>(r) << 16) |
                                                 (static_cast<uint32_t>(g) << 8) |
                                                 static_cast<uint32_t>(b);
                                }
                            } else { // RGB
                                uint8_t r = static_cast<uint8_t>(shape_reader.read_bits(8));
                                uint8_t g = static_cast<uint8_t>(shape_reader.read_bits(8));
                                uint8_t b = static_cast<uint8_t>(shape_reader.read_bits(8));
                                if (i == 0) {
                                    fill_color = (static_cast<uint32_t>(r) << 16) |
                                                 (static_cast<uint32_t>(g) << 8) |
                                                 static_cast<uint32_t>(b);
                                }
                            }
                        } else if (fill_type == 0x10 || fill_type == 0x12 || fill_type == 0x13) {
                            read_swf_matrix(shape_reader);
                            shape_reader.read_bits(8);
                            uint8_t num_grads = static_cast<uint8_t>(shape_reader.read_bits(8));
                            for (uint8_t g_idx = 0; g_idx < num_grads && !shape_reader.is_eof(); ++g_idx) {
                                shape_reader.read_bits(8);
                                shape_reader.read_bits(tag_type == 32 ? 32 : 24);
                            }
                        } else if (fill_type == 0x40 || fill_type == 0x41 || fill_type == 0x42 || fill_type == 0x43) {
                            shape_reader.read_bits(16);
                            read_swf_matrix(shape_reader);
                        }
                    }
                };

                auto parse_line_style_array = [&shape_reader, tag_type]() {
                    if (shape_reader.is_eof()) return;
                    uint32_t line_count = shape_reader.read_bits(8);
                    if (line_count == 0xFF) {
                        line_count = shape_reader.read_bits(16);
                    }
                    for (uint32_t i = 0; i < line_count && !shape_reader.is_eof(); ++i) {
                        shape_reader.read_bits(16);
                        if (tag_type == 32) {
                            shape_reader.read_bits(32);
                        } else {
                            shape_reader.read_bits(24);
                        }
                    }
                };

                parse_fill_style_array(parse_fill_style_array);
                parse_line_style_array();

                uint8_t num_fill_bits = static_cast<uint8_t>(shape_reader.read_bits(4));
                uint8_t num_line_bits = static_cast<uint8_t>(shape_reader.read_bits(4));

                std::vector<Point2D> polygon_vertices;
                float cur_x = 0.0f;
                float cur_y = 0.0f;

                int max_records = 10000;
                while (max_records-- > 0 && !shape_reader.is_eof()) {
                    uint8_t type_flag = static_cast<uint8_t>(shape_reader.read_bits(1));
                    if (type_flag == 0) { // StyleChange or EndShape
                        uint8_t state_new_styles  = static_cast<uint8_t>(shape_reader.read_bits(1));
                        uint8_t state_line_style  = static_cast<uint8_t>(shape_reader.read_bits(1));
                        uint8_t state_fill_style1 = static_cast<uint8_t>(shape_reader.read_bits(1));
                        uint8_t state_fill_style0 = static_cast<uint8_t>(shape_reader.read_bits(1));
                        uint8_t state_move_to     = static_cast<uint8_t>(shape_reader.read_bits(1));

                        if (!state_new_styles && !state_line_style && !state_fill_style1 && !state_fill_style0 && !state_move_to) {
                            break; // EndShapeRecord
                        }

                        if (state_move_to) {
                            uint8_t move_bits = static_cast<uint8_t>(shape_reader.read_bits(5));
                            int32_t move_x = shape_reader.read_sbits(move_bits);
                            int32_t move_y = shape_reader.read_sbits(move_bits);
                            cur_x = static_cast<float>(move_x) / 20.0f;
                            cur_y = static_cast<float>(move_y) / 20.0f;
                            if (polygon_vertices.empty()) {
                                polygon_vertices.push_back({cur_x, cur_y});
                            }
                        }
                        if (state_fill_style0 && num_fill_bits > 0) {
                            shape_reader.read_bits(num_fill_bits);
                        }
                        if (state_fill_style1 && num_fill_bits > 0) {
                            shape_reader.read_bits(num_fill_bits);
                        }
                        if (state_line_style && num_line_bits > 0) {
                            shape_reader.read_bits(num_line_bits);
                        }
                        if (state_new_styles) {
                            parse_fill_style_array(parse_fill_style_array);
                            parse_line_style_array();
                            num_fill_bits = static_cast<uint8_t>(shape_reader.read_bits(4));
                            num_line_bits = static_cast<uint8_t>(shape_reader.read_bits(4));
                        }
                    } else { // Edge Record
                        uint8_t straight_flag = static_cast<uint8_t>(shape_reader.read_bits(1));
                        if (straight_flag == 1) { // StraightEdgeRecord
                            uint8_t num_bits = static_cast<uint8_t>(shape_reader.read_bits(4));
                            uint8_t n_bits = num_bits + 2;
                            uint8_t general_line_flag = static_cast<uint8_t>(shape_reader.read_bits(1));
                            int32_t delta_x = 0;
                            int32_t delta_y = 0;
                            if (general_line_flag == 1) {
                                delta_x = shape_reader.read_sbits(n_bits);
                                delta_y = shape_reader.read_sbits(n_bits);
                            } else {
                                uint8_t vert_line_flag = static_cast<uint8_t>(shape_reader.read_bits(1));
                                if (vert_line_flag == 1) {
                                    delta_y = shape_reader.read_sbits(n_bits);
                                } else {
                                    delta_x = shape_reader.read_sbits(n_bits);
                                }
                            }
                            cur_x += static_cast<float>(delta_x) / 20.0f;
                            cur_y += static_cast<float>(delta_y) / 20.0f;
                            polygon_vertices.push_back({cur_x, cur_y});
                        } else { // CurvedEdgeRecord
                            uint8_t num_bits = static_cast<uint8_t>(shape_reader.read_bits(4));
                            uint8_t n_bits = num_bits + 2;
                            int32_t control_delta_x = shape_reader.read_sbits(n_bits);
                            int32_t control_delta_y = shape_reader.read_sbits(n_bits);
                            int32_t anchor_delta_x  = shape_reader.read_sbits(n_bits);
                            int32_t anchor_delta_y  = shape_reader.read_sbits(n_bits);

                            Point2D p0 = {cur_x, cur_y};
                            Point2D p1 = {cur_x + static_cast<float>(control_delta_x) / 20.0f,
                                          cur_y + static_cast<float>(control_delta_y) / 20.0f};
                            Point2D p2 = {p1.x + static_cast<float>(anchor_delta_x) / 20.0f,
                                          p1.y + static_cast<float>(anchor_delta_y) / 20.0f};

                            VectorTessellator::subdivide_quadratic_bezier(p0, p1, p2, polygon_vertices);
                            cur_x = p2.x;
                            cur_y = p2.y;
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
                shape.polygon_vertices = std::move(polygon_vertices);

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
                m_current_frame_builder.sound_stream_block.assign(payload, payload + tag_len);
            }
        } else if (tag_type == 26) { // TagPlaceObject2
            if (tag_len >= 3) {
                uint8_t flags = uncompressed_data[tag_offset];
                bool move = (flags & 0x01) != 0; (void)move;
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

                Matrix2D mat;
                if (has_matrix && p_idx < tag_offset + tag_len) {
                    BitReader mat_reader(uncompressed_data.data() + p_idx, (tag_offset + tag_len) - p_idx);
                    mat = read_swf_matrix(mat_reader);
                }

                SWFPlaceCommand cmd;
                cmd.depth = depth;
                cmd.character_id = character_id;
                cmd.transform_x = static_cast<int32_t>(mat.tx);
                cmd.transform_y = static_cast<int32_t>(mat.ty);
                cmd.move = move;
                cmd.has_character = has_character;
                cmd.has_matrix = has_matrix;
                cmd.matrix = mat;

                m_current_frame_builder.place_commands.push_back(cmd);
            }
        } else if (tag_type == 28) { // TagRemoveObject2
            if (tag_len >= 2) {
                uint16_t depth = static_cast<uint16_t>(uncompressed_data[tag_offset]) |
                                (static_cast<uint16_t>(uncompressed_data[tag_offset + 1]) << 8);
                SWFRemoveCommand cmd;
                cmd.depth = depth;
                m_current_frame_builder.remove_commands.push_back(cmd);
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
                            bool move = (flags & 0x01) != 0; (void)move;
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

                            Matrix2D mat;
                            if (has_mat && p_idx < sub_offset + sub_len) {
                                BitReader mat_reader(uncompressed_data.data() + p_idx, (sub_offset + sub_len) - p_idx);
                                mat = read_swf_matrix(mat_reader);
                            }

                            DisplayObject sub_obj;
                            sub_obj.depth = depth;
                            sub_obj.character_id = char_id;
                            sub_obj.transform_x = static_cast<int32_t>(mat.tx);
                            sub_obj.transform_y = static_cast<int32_t>(mat.ty);
                            sub_obj.matrix = mat;

                            auto existing = std::find_if(sprite.sub_objects.begin(), sprite.sub_objects.end(),
                                [depth](const DisplayObject& obj) { return obj.depth == depth; });
                            if (existing != sprite.sub_objects.end()) {
                                if (has_char && char_id != 0) existing->character_id = char_id;
                                if (has_mat) {
                                    existing->transform_x = sub_obj.transform_x;
                                    existing->transform_y = sub_obj.transform_y;
                                    existing->matrix = mat;
                                }
                            } else {
                                sprite.sub_objects.push_back(sub_obj);
                            }
                        }
                    } else if (sub_type == 28) { // TagRemoveObject2 inside sprite
                        if (sub_len >= 2) {
                            uint16_t depth = static_cast<uint16_t>(uncompressed_data[sub_offset]) |
                                            (static_cast<uint16_t>(uncompressed_data[sub_offset + 1]) << 8);
                            sprite.sub_objects.erase(
                                std::remove_if(sprite.sub_objects.begin(), sprite.sub_objects.end(),
                                    [depth](const DisplayObject& obj) { return obj.depth == depth; }),
                                sprite.sub_objects.end()
                            );
                        }
                    }

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
            if (!m_current_frame_builder.place_commands.empty() ||
                !m_current_frame_builder.remove_commands.empty() ||
                !m_current_frame_builder.sound_stream_block.empty()) {
                m_timeline_frames.push_back(std::move(m_current_frame_builder));
                m_current_frame_builder = SWFFrame{};
            }
            break;
        }
    }

    if (!m_current_frame_builder.place_commands.empty() ||
        !m_current_frame_builder.remove_commands.empty() ||
        !m_current_frame_builder.sound_stream_block.empty()) {
        m_timeline_frames.push_back(std::move(m_current_frame_builder));
        m_current_frame_builder = SWFFrame{};
    }

    return true;
}

void SWFParser::apply_frame(size_t frame_index) {
    if (frame_index >= m_timeline_frames.size()) return;

    if (frame_index == 0 && m_timeline_frames.size() > 1) {
        m_display_list.clear_active_objects();
    }

    const SWFFrame& frame = m_timeline_frames[frame_index];

    for (const auto& remove_cmd : frame.remove_commands) {
        m_display_list.remove_object(remove_cmd.depth);
    }

    for (const auto& place_cmd : frame.place_commands) {
        m_display_list.place_object_matrix(
            place_cmd.depth,
            place_cmd.character_id,
            place_cmd.matrix,
            place_cmd.has_character
        );
    }

    if (!frame.sound_stream_block.empty()) {
        auto decoded_samples = m_stream_decoder.decode_block(
            frame.sound_stream_block.data(),
            frame.sound_stream_block.size()
        );
        if (m_audio_mixer && !decoded_samples.empty()) {
            m_audio_mixer->queue_samples(decoded_samples.data(), decoded_samples.size());
        }
    }
}
