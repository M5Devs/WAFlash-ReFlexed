#ifndef AUDIO_STREAM_DECODER_H
#define AUDIO_STREAM_DECODER_H

#include <cstdint>
#include <cstddef>
#include <vector>

enum class SoundFormat : uint8_t {
    UncompressedNativeLE = 0,
    ADPCM = 1,
    MP3 = 2,
    UncompressedBE = 3,
    Nellymoser = 6
};

struct SoundStreamHeader {
    SoundFormat format{SoundFormat::MP3};
    uint32_t sample_rate{44100};
    bool is_16bit{true};
    bool is_stereo{true};
    uint16_t samples_per_frame{0};
    int16_t latency_seek{0};
    bool active{false};
};

class AudioStreamDecoder {
public:
    AudioStreamDecoder();
    ~AudioStreamDecoder();

    void initialize(const SoundStreamHeader& header);
    std::vector<int16_t> decode_block(const uint8_t* compressed_data, size_t data_size);
    void reset();

    const SoundStreamHeader& get_header() const { return m_header; }

private:
    SoundStreamHeader m_header{};
    bool m_latency_compensated{false};
    void* m_mp3_decoder_state{nullptr};

    std::vector<int16_t> resample_to_44k_stereo(const int16_t* pcm_in, size_t in_samples, uint32_t in_rate, bool is_stereo);
};

#endif // AUDIO_STREAM_DECODER_H
