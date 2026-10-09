#include "display_list.h"
#include "audio_mixer.h"
#include "swf_parser.h"
#include "wasm_api.h"
#include <cassert>
#include <cstdio>
#include <vector>

void test_place_object2_move_preservation() {
    printf("[TEST] Testing PlaceObject2 Move Preservation...\n");
    DisplayList dl;

    // Register a dummy shape definition (character_id 42)
    SWFShapeDefinition shape;
    shape.character_id = 42;
    shape.fill_color_xrgb = 0xFF0000;
    dl.register_shape(shape);

    // Initial PlaceObject: depth 1, character_id 42, position (10, 20) with has_character = true
    Matrix2D initial_mat;
    initial_mat.a = 2.0f; initial_mat.d = 2.0f;
    initial_mat.tx = 10.0f; initial_mat.ty = 20.0f;
    dl.place_object_matrix(1, 42, initial_mat, true);

    const auto& active1 = dl.get_active_objects();
    assert(active1.find(1) != active1.end());
    assert(active1.at(1).character_id == 42);
    assert(active1.at(1).matrix.a == 2.0f);
    assert(active1.at(1).transform_x == 10);
    assert(active1.at(1).transform_y == 20);

    // Subsequent TagPlaceObject2 Move: depth 1, character_id 0, position (50, 60), has_character = false
    Matrix2D move_mat;
    move_mat.a = 2.0f; move_mat.d = 2.0f;
    move_mat.tx = 50.0f; move_mat.ty = 60.0f;
    dl.place_object_matrix(1, 0, move_mat, false);

    const auto& active2 = dl.get_active_objects();
    assert(active2.find(1) != active2.end());
    // The character_id must remain 42!
    assert(active2.at(1).character_id == 42);
    assert(active2.at(1).transform_x == 50);
    assert(active2.at(1).transform_y == 60);

    printf("  [PASS] PlaceObject2 move preserved character_id 42 across matrix updates.\n");
}

void test_audio_mixer_reopen_buffer() {
    printf("[TEST] Testing AudioMixer reopen_buffer()...\n");
    AudioMixer mixer;

    // Enqueue PCM samples and generate a tone to advance phase
    std::vector<int16_t> samples(1000, 1234);
    mixer.queue_samples(samples.data(), samples.size());
    mixer.generate_tone(ToneType::Sine, 440.0f, 0.1f, 0.8f);

    assert(mixer.get_queued_frames() > 0);

    // Call reopen_buffer()
    mixer.reopen_buffer();

    assert(mixer.get_queued_frames() == 0);

    // Confirm that buffer renders 0 samples (silence)
    int16_t buf[256];
    size_t rendered = mixer.render_frame(buf, 128);
    assert(rendered == 128);
    for (int i = 0; i < 256; ++i) {
        assert(buf[i] == 0);
    }

    // Call WASM API wrapper
    wasm_reopen_audio_buffer();

    printf("  [PASS] AudioMixer reopen_buffer() cleared queue and reset phase cleanly.\n");
}

void test_preloader_frame_properties() {
    printf("[TEST] Testing Preloader Frame Properties Reporting...\n");

    // Construct a minimal SWF with 3 frames (TagShowFrame x 3)
    std::vector<uint8_t> dummy_swf = {
        'F', 'W', 'S', 15,
        22, 0, 0, 0,                   // File length = 22 bytes
        0x00,                          // Rect 0x0 twips
        0x00, 0x3C,                    // 60 fps
        0x03, 0x00,                    // 3 frames declared
        0x40, 0x00,                    // TagShowFrame (Frame 1)
        0x40, 0x00,                    // TagShowFrame (Frame 2)
        0x40, 0x00,                    // TagShowFrame (Frame 3)
        0x00, 0x00                     // TagEnd
    };

    SWFParser parser;
    bool ok = parser.parse(dummy_swf.data(), dummy_swf.size());
    assert(ok);
    assert(parser.get_header().frame_count == 3);
    assert(parser.get_show_frame_count() == 3);

    printf("  [PASS] Preloader frame property reporting returned full frame count (3).\n");
}

int main() {
    printf("=== Running Runtime Patches Unit Tests ===\n");
    test_place_object2_move_preservation();
    test_audio_mixer_reopen_buffer();
    test_preloader_frame_properties();
    printf("=== All Runtime Patches Unit Tests Passed! ===\n");
    return 0;
}
