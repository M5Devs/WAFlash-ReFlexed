#ifndef SWF_PARSER_H
#define SWF_PARSER_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include "display_list.h"
#include "audio_mixer.h"
#include "audio_stream_decoder.h"

struct SWFHeader {
    char signature[3];
    uint8_t version;
    uint32_t file_length;
    uint32_t width_px;
    uint32_t height_px;
    float frame_rate;
    uint16_t frame_count;
    uint32_t background_color_xrgb;
};

struct ABCTag {
    uint32_t flags;
    std::string name;
    std::vector<uint8_t> bytecode_data;
};

struct SWFSoundStreamHeader {
    uint8_t stream_format{0};  // 0: Uncompressed LE, 1: ADPCM, 2: MP3, 3: Uncompressed BE
    uint32_t sample_rate{44100}; // 5512, 11025, 22050, 44100
    bool is_16bit{true};
    bool is_stereo{false};
    uint16_t sample_count_per_frame{0};
    int16_t latency_seek{0};
    bool is_active{false};
};

struct SWFPlaceCommand {
    uint16_t depth{0};
    uint16_t character_id{0};
    int32_t transform_x{0};
    int32_t transform_y{0};
    bool has_character{false};
    bool has_matrix{false};
    Matrix2D matrix{};
};

struct SWFRemoveCommand {
    uint16_t depth{0};
};

struct SWFFrame {
    std::vector<SWFPlaceCommand>  place_commands;
    std::vector<SWFRemoveCommand> remove_commands;
    std::vector<uint8_t>          sound_stream_block; // Compressed audio payload for this frame
};

class SWFParser {
public:
    SWFParser();
    ~SWFParser();

    bool parse(const uint8_t* data, size_t size);
    void apply_frame(size_t frame_index);

    void set_audio_mixer(AudioMixer* mixer) { m_audio_mixer = mixer; }
    AudioMixer* get_audio_mixer() const { return m_audio_mixer; }

    const SWFHeader& get_header() const { return m_header; }
    const std::vector<ABCTag>& get_abc_tags() const { return m_abc_tags; }
    const std::vector<size_t>& get_show_frame_positions() const { return m_show_frame_positions; }
    size_t get_show_frame_count() const { return m_show_frame_positions.size(); }
    const std::vector<SWFFrame>& get_timeline_frames() const { return m_timeline_frames; }
    const DisplayList& get_display_list() const { return m_display_list; }
    DisplayList& get_display_list() { return m_display_list; }
    const SWFSoundStreamHeader& get_sound_stream_header() const { return m_sound_stream_header; }
    AudioStreamDecoder& get_stream_decoder() { return m_stream_decoder; }
    const AudioStreamDecoder& get_stream_decoder() const { return m_stream_decoder; }

private:
    SWFHeader m_header;
    SWFSoundStreamHeader m_sound_stream_header;
    std::vector<ABCTag> m_abc_tags;
    std::vector<size_t> m_show_frame_positions;
    SWFFrame m_current_frame_builder;
    std::vector<SWFFrame> m_timeline_frames;
    DisplayList m_display_list;
    AudioMixer* m_audio_mixer{nullptr};
    AudioStreamDecoder m_stream_decoder;
};

#endif // SWF_PARSER_H
