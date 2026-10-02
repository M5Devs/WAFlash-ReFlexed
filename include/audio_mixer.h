#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

#include <cstdint>
#include <cstddef>
#include <vector>

enum class ToneType {
    Silence,
    Sine,
    Square
};

class AudioMixer {
public:
    static constexpr uint32_t SAMPLE_RATE = 44100;
    static constexpr uint32_t CHANNELS = 2; // Stereo

    AudioMixer();
    ~AudioMixer();

    void reset();

    // Enqueue interleaved PCM audio samples (left, right, left, right...)
    // sample_count is total int16_t samples (2 * frames for stereo)
    void queue_samples(const int16_t* samples, size_t sample_count);

    // Synthesize tone into internal queue
    void generate_tone(ToneType type, float frequency_hz, float duration_sec, float volume = 0.5f);

    // Render audio frame into out_buffer. Returns number of frames generated.
    // out_buffer must have capacity for at least (num_frames * CHANNELS) int16_t samples.
    size_t generate_audio_frame(int16_t* out_buffer, size_t num_frames);
    size_t render_frame(int16_t* out_buffer, size_t num_frames);

    // Queue size in frames (total int16 samples / CHANNELS)
    size_t get_queued_frames() const;

    // Clear internal staging buffer
    void clear_buffer();

private:
    std::vector<int16_t> m_ring_buffer;
    float m_phase;
};

#endif // AUDIO_MIXER_H
