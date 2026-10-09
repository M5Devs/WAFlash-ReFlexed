#include "audio_mixer.h"
#include <cmath>
#include <cstring>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

AudioMixer::AudioMixer() : m_phase(0.0f) {
}

AudioMixer::~AudioMixer() {
}

void AudioMixer::reset() {
    m_ring_buffer.clear();
    m_phase = 0.0f;
}

void AudioMixer::reopen_buffer() {
    clear_buffer();
    m_phase = 0.0f;
}

void AudioMixer::queue_samples(const int16_t* samples, size_t sample_count) {
    if (!samples || sample_count == 0) return;
    m_ring_buffer.insert(m_ring_buffer.end(), samples, samples + sample_count);
}

void AudioMixer::generate_tone(ToneType type, float frequency_hz, float duration_sec, float volume) {
    if (duration_sec <= 0.0f) return;

    size_t total_frames = static_cast<size_t>(SAMPLE_RATE * duration_sec);
    if (total_frames == 0) return;

    std::vector<int16_t> tone_samples;
    tone_samples.reserve(total_frames * CHANNELS);

    volume = std::clamp(volume, 0.0f, 1.0f);
    float phase_increment = (2.0f * static_cast<float>(M_PI) * frequency_hz) / static_cast<float>(SAMPLE_RATE);

    for (size_t i = 0; i < total_frames; ++i) {
        int16_t val = 0;

        switch (type) {
            case ToneType::Silence:
                val = 0;
                break;
            case ToneType::Sine: {
                float sample_flt = std::sin(m_phase);
                val = static_cast<int16_t>(sample_flt * volume * 32767.0f);
                break;
            }
            case ToneType::Square: {
                float sample_flt = (std::sin(m_phase) >= 0.0f) ? 1.0f : -1.0f;
                val = static_cast<int16_t>(sample_flt * volume * 32767.0f);
                break;
            }
        }

        m_phase += phase_increment;
        if (m_phase >= 2.0f * static_cast<float>(M_PI)) {
            m_phase -= 2.0f * static_cast<float>(M_PI);
        }

        // Stereo PCM (Left, Right)
        tone_samples.push_back(val);
        tone_samples.push_back(val);
    }

    queue_samples(tone_samples.data(), tone_samples.size());
}

size_t AudioMixer::generate_audio_frame(int16_t* out_buffer, size_t num_frames) {
    if (!out_buffer || num_frames == 0) return 0;

    size_t samples_requested = num_frames * CHANNELS;
    size_t samples_available = m_ring_buffer.size();

    size_t samples_to_copy = std::min(samples_requested, samples_available);

    if (samples_to_copy > 0) {
        std::memcpy(out_buffer, m_ring_buffer.data(), samples_to_copy * sizeof(int16_t));
        m_ring_buffer.erase(m_ring_buffer.begin(), m_ring_buffer.begin() + samples_to_copy);
    }

    // Fill any remaining buffer requirement with silence (0) to avoid underruns
    if (samples_to_copy < samples_requested) {
        std::memset(out_buffer + samples_to_copy, 0, (samples_requested - samples_to_copy) * sizeof(int16_t));
    }

    return num_frames;
}

size_t AudioMixer::get_queued_frames() const {
    return m_ring_buffer.size() / CHANNELS;
}

void AudioMixer::clear_buffer() {
    m_ring_buffer.clear();
}

size_t AudioMixer::render_frame(int16_t* out_buffer, size_t num_frames) {
    return generate_audio_frame(out_buffer, num_frames);
}
