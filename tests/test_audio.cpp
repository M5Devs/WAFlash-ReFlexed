#include "audio_mixer.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <cmath>

void test_audio_mixer_initialization() {
    AudioMixer mixer;
    assert(AudioMixer::SAMPLE_RATE == 44100);
    assert(AudioMixer::CHANNELS == 2);
    assert(mixer.get_queued_frames() == 0);
    std::cout << "[PASS] test_audio_mixer_initialization" << std::endl;
}

void test_queue_and_drain_samples() {
    AudioMixer mixer;
    int16_t sample_data[8] = { 100, 100, 200, 200, 300, 300, 400, 400 }; // 4 stereo frames
    mixer.queue_samples(sample_data, 8);
    assert(mixer.get_queued_frames() == 4);

    int16_t out_data[8] = { 0 };
    size_t rendered_frames = mixer.generate_audio_frame(out_data, 2); // Request 2 frames (4 samples)
    assert(rendered_frames == 2);
    assert(mixer.get_queued_frames() == 2);
    assert(out_data[0] == 100 && out_data[1] == 100);
    assert(out_data[2] == 200 && out_data[3] == 200);

    // Drain remaining 2 frames
    rendered_frames = mixer.generate_audio_frame(out_data, 2);
    assert(rendered_frames == 2);
    assert(mixer.get_queued_frames() == 0);
    assert(out_data[0] == 300 && out_data[1] == 300);
    assert(out_data[2] == 400 && out_data[3] == 400);

    std::cout << "[PASS] test_queue_and_drain_samples" << std::endl;
}

void test_underrun_silence_padding() {
    AudioMixer mixer;
    int16_t sample_data[4] = { 500, 500, 600, 600 }; // 2 frames
    mixer.queue_samples(sample_data, 4);

    int16_t out_data[10] = { -1 }; // Request 5 frames (10 samples)
    size_t rendered_frames = mixer.render_frame(out_data, 5);
    assert(rendered_frames == 5);
    assert(mixer.get_queued_frames() == 0);

    // First 2 frames should match queued samples
    assert(out_data[0] == 500 && out_data[1] == 500);
    assert(out_data[2] == 600 && out_data[3] == 600);

    // Remaining 3 frames (samples 4..9) must be zeroed (silence)
    for (size_t i = 4; i < 10; ++i) {
        assert(out_data[i] == 0);
    }

    std::cout << "[PASS] test_underrun_silence_padding" << std::endl;
}

void test_tone_synthesis() {
    AudioMixer mixer;
    // Generate 440 Hz sine wave for 0.1 sec (4410 frames)
    mixer.generate_tone(ToneType::Sine, 440.0f, 0.1f, 0.8f);
    assert(mixer.get_queued_frames() == 4410);

    std::vector<int16_t> out_buffer(4410 * 2, 0);
    size_t rendered = mixer.generate_audio_frame(out_buffer.data(), 4410);
    assert(rendered == 4410);
    assert(mixer.get_queued_frames() == 0);

    // Verify non-zero stereo PCM frames were synthesized
    bool found_non_zero = false;
    for (size_t i = 0; i < out_buffer.size(); i += 2) {
        assert(out_buffer[i] == out_buffer[i + 1]); // Stereo L == R for mono tone synthesis
        if (out_buffer[i] != 0) {
            found_non_zero = true;
        }
    }
    assert(found_non_zero);

    std::cout << "[PASS] test_tone_synthesis" << std::endl;
}

void test_clear_buffer() {
    AudioMixer mixer;
    mixer.generate_tone(ToneType::Square, 220.0f, 0.5f, 0.5f);
    assert(mixer.get_queued_frames() > 0);
    mixer.clear_buffer();
    assert(mixer.get_queued_frames() == 0);
    std::cout << "[PASS] test_clear_buffer" << std::endl;
}

int main() {
    std::cout << "Running Audio Subsystem Tests..." << std::endl;
    test_audio_mixer_initialization();
    test_queue_and_drain_samples();
    test_underrun_silence_padding();
    test_tone_synthesis();
    test_clear_buffer();
    std::cout << "All Audio Subsystem Tests Passed Successfully!" << std::endl;
    return 0;
}
