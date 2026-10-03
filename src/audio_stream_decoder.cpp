#include "audio_stream_decoder.h"
#include "audio_mixer.h"

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include <cstdlib>
#include <cstring>
#include <algorithm>

AudioStreamDecoder::AudioStreamDecoder() = default;

AudioStreamDecoder::~AudioStreamDecoder() {
    reset();
}

void AudioStreamDecoder::reset() {
    if (m_mp3_decoder_state) {
        mp3dec_t* dec = reinterpret_cast<mp3dec_t*>(m_mp3_decoder_state);
        delete dec;
        m_mp3_decoder_state = nullptr;
    }
    m_latency_compensated = false;
    m_header = SoundStreamHeader{};
}

void AudioStreamDecoder::initialize(const SoundStreamHeader& header) {
    reset();
    m_header = header;
    m_header.active = true;
    m_latency_compensated = false;

    if (m_header.format == SoundFormat::MP3) {
        mp3dec_t* dec = new mp3dec_t();
        mp3dec_init(dec);
        m_mp3_decoder_state = reinterpret_cast<void*>(dec);
    }
}

std::vector<int16_t> AudioStreamDecoder::resample_to_44k_stereo(
    const int16_t* pcm_in, size_t in_samples, uint32_t in_rate, bool is_stereo) {

    std::vector<int16_t> out_pcm;
    if (!pcm_in || in_samples == 0) return out_pcm;

    size_t in_frames = is_stereo ? (in_samples / 2) : in_samples;
    if (in_frames == 0) return out_pcm;

    if (in_rate == 0) in_rate = 44100;

    double ratio = 44100.0 / static_cast<double>(in_rate);
    size_t target_frames = static_cast<size_t>(static_cast<double>(in_frames) * ratio);
    if (target_frames == 0) target_frames = 1;

    out_pcm.reserve(target_frames * AudioMixer::CHANNELS);

    for (size_t f = 0; f < target_frames; ++f) {
        double src_frame_idx = static_cast<double>(f) / ratio;
        size_t idx0 = static_cast<size_t>(src_frame_idx);
        size_t idx1 = std::min(idx0 + 1, in_frames - 1);
        double frac = src_frame_idx - static_cast<double>(idx0);

        int16_t l0, r0, l1, r1;
        if (is_stereo) {
            l0 = pcm_in[idx0 * 2];
            r0 = pcm_in[idx0 * 2 + 1];
            l1 = pcm_in[idx1 * 2];
            r1 = pcm_in[idx1 * 2 + 1];
        } else {
            l0 = r0 = pcm_in[idx0];
            l1 = r1 = pcm_in[idx1];
        }

        int16_t out_l = static_cast<int16_t>(l0 + frac * (l1 - l0));
        int16_t out_r = static_cast<int16_t>(r0 + frac * (r1 - r0));

        out_pcm.push_back(out_l);
        out_pcm.push_back(out_r);
    }

    return out_pcm;
}

std::vector<int16_t> AudioStreamDecoder::decode_block(const uint8_t* compressed_data, size_t data_size) {
    std::vector<int16_t> decoded_pcm;
    if (!compressed_data || data_size == 0) return decoded_pcm;

    if (m_header.format == SoundFormat::MP3) {
        if (!m_mp3_decoder_state) return decoded_pcm;

        mp3dec_t* dec = reinterpret_cast<mp3dec_t*>(m_mp3_decoder_state);
        mp3dec_frame_info_t info;
        int16_t pcm_buf[MINIMP3_MAX_SAMPLES_PER_FRAME];

        size_t bytes_left = data_size;
        const uint8_t* stream_ptr = compressed_data;

        // Skip 2-byte MP3 sample seek header present in SWF MP3 stream blocks
        if (bytes_left >= 2) {
            stream_ptr += 2;
            bytes_left -= 2;
        }

        while (bytes_left > 0) {
            int samples = mp3dec_decode_frame(dec, stream_ptr, static_cast<int>(bytes_left), pcm_buf, &info);
            if (info.frame_bytes == 0) break;

            stream_ptr += info.frame_bytes;
            bytes_left -= info.frame_bytes;

            if (samples > 0) {
                size_t total_samples = static_cast<size_t>(samples) * static_cast<size_t>(info.channels);
                auto resampled = resample_to_44k_stereo(pcm_buf, total_samples, static_cast<uint32_t>(info.hz), info.channels == 2);
                decoded_pcm.insert(decoded_pcm.end(), resampled.begin(), resampled.end());
            }
        }

        // Apply Latency Compensation on initial decoded frame
        if (!m_latency_compensated && m_header.latency_seek > 0) {
            size_t skip_samples = static_cast<size_t>(m_header.latency_seek) * AudioMixer::CHANNELS;
            if (decoded_pcm.size() > skip_samples) {
                decoded_pcm.erase(decoded_pcm.begin(), decoded_pcm.begin() + skip_samples);
            } else {
                decoded_pcm.clear();
            }
            m_latency_compensated = true;
        }
    } else if (m_header.format == SoundFormat::UncompressedNativeLE || m_header.format == SoundFormat::UncompressedBE) {
        size_t sample_size_bytes = m_header.is_16bit ? 2 : 1;
        size_t channels = m_header.is_stereo ? 2 : 1;
        size_t frame_bytes = sample_size_bytes * channels;
        size_t total_input_frames = data_size / frame_bytes;

        std::vector<int16_t> pcm_buf;
        pcm_buf.reserve(total_input_frames * channels);

        for (size_t f = 0; f < total_input_frames; ++f) {
            int16_t left = 0, right = 0;
            size_t frame_offset = f * frame_bytes;

            if (m_header.is_16bit) {
                if (m_header.format == SoundFormat::UncompressedNativeLE) {
                    left = static_cast<int16_t>(compressed_data[frame_offset] | (compressed_data[frame_offset + 1] << 8));
                    if (channels == 2) {
                        right = static_cast<int16_t>(compressed_data[frame_offset + 2] | (compressed_data[frame_offset + 3] << 8));
                    } else {
                        right = left;
                    }
                } else { // BE
                    left = static_cast<int16_t>((compressed_data[frame_offset] << 8) | compressed_data[frame_offset + 1]);
                    if (channels == 2) {
                        right = static_cast<int16_t>((compressed_data[frame_offset + 2] << 8) | compressed_data[frame_offset + 3]);
                    } else {
                        right = left;
                    }
                }
            } else { // 8-bit unsigned -> int16
                left = static_cast<int16_t>((static_cast<int32_t>(compressed_data[frame_offset]) - 128) * 256);
                if (channels == 2) {
                    right = static_cast<int16_t>((static_cast<int32_t>(compressed_data[frame_offset + 1]) - 128) * 256);
                } else {
                    right = left;
                }
            }

            pcm_buf.push_back(left);
            if (channels == 2) {
                pcm_buf.push_back(right);
            }
        }

        decoded_pcm = resample_to_44k_stereo(pcm_buf.data(), pcm_buf.size(), m_header.sample_rate, m_header.is_stereo);
    }

    return decoded_pcm;
}
