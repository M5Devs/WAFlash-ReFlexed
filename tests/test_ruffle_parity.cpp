#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include "swf_parser.h"
#include "display_list.h"
#include "audio_mixer.h"

// Helper class to construct bitstream for testing BitReader and read_swf_matrix
class TestBitWriter {
public:
    std::vector<uint8_t> buffer;
    uint8_t current_byte = 0;
    int bit_count = 0;

    void write_bits(uint32_t val, uint8_t num_bits) {
        for (int i = num_bits - 1; i >= 0; --i) {
            uint8_t bit = (val >> i) & 1;
            current_byte = (current_byte << 1) | bit;
            bit_count++;
            if (bit_count == 8) {
                buffer.push_back(current_byte);
                current_byte = 0;
                bit_count = 0;
            }
        }
    }

    void align_byte() {
        if (bit_count > 0) {
            current_byte <<= (8 - bit_count);
            buffer.push_back(current_byte);
            current_byte = 0;
            bit_count = 0;
        }
    }
};

void test_place_object_move_flag() {
    std::cout << "[Test 1] TagPlaceObject2 has_character == false (Move flag)..." << std::endl;

    DisplayList dl;

    // Define a dummy shape with character_id = 42
    SWFShapeDefinition shape;
    shape.character_id = 42;
    shape.fill_color_xrgb = 0x00FF0000;
    shape.x_min = 0; shape.x_max = 100;
    shape.y_min = 0; shape.y_max = 100;
    dl.register_shape(shape);

    // Initial PlaceObject at depth 5 with character_id = 42, translation (10, 20)
    Matrix2D mat1;
    mat1.tx = 10.0f;
    mat1.ty = 20.0f;
    dl.place_object_matrix(5, 42, mat1, true);

    const auto& active_1 = dl.get_active_objects();
    assert(active_1.find(5) != active_1.end());
    assert(active_1.at(5).character_id == 42);
    assert(std::abs(active_1.at(5).matrix.tx - 10.0f) < 0.001f);
    assert(std::abs(active_1.at(5).matrix.ty - 20.0f) < 0.001f);

    // Subsequent PlaceObject with move flag (has_character == false) at depth 5 with translation (150, 250)
    Matrix2D mat2;
    mat2.a = 2.0f;
    mat2.d = 2.0f;
    mat2.tx = 150.0f;
    mat2.ty = 250.0f;
    dl.place_object_matrix(5, 0, mat2, false);

    const auto& active_2 = dl.get_active_objects();
    assert(active_2.find(5) != active_2.end());
    // CRITICAL: character_id must remain 42 even though has_character == false
    assert(active_2.at(5).character_id == 42);
    assert(std::abs(active_2.at(5).matrix.a - 2.0f) < 0.001f);
    assert(std::abs(active_2.at(5).matrix.d - 2.0f) < 0.001f);
    assert(std::abs(active_2.at(5).matrix.tx - 150.0f) < 0.001f);
    assert(std::abs(active_2.at(5).matrix.ty - 250.0f) < 0.001f);

    // Root stage scene graph check
    auto root = dl.get_root_stage();
    assert(root != nullptr);
    assert(root->children.find(5) != root->children.end());
    assert(root->children[5]->character_id == 42);
    assert(std::abs(root->children[5]->local_matrix.a - 2.0f) < 0.001f);
    assert(std::abs(root->children[5]->local_matrix.tx - 150.0f) < 0.001f);

    std::cout << "  -> PASSED" << std::endl;
}

void test_fixed_point_matrix_decoding() {
    std::cout << "[Test 2] 16.16 Fixed-Point Matrix Decoding..." << std::endl;

    TestBitWriter writer;

    // HasScale = 1
    writer.write_bits(1, 1);
    // NScaleBits = 19 (19 bits to fit signed +131072)
    writer.write_bits(19, 5);
    // ScaleX = 131072 (2.0 in 16.16 signed)
    writer.write_bits(131072, 19);
    // ScaleY = 65536 (1.0 in 16.16 signed)
    writer.write_bits(65536, 19);

    // HasRotate = 1
    writer.write_bits(1, 1);
    // NRotateBits = 18
    writer.write_bits(18, 5);
    // RotateSkew0 = 32768 (0.5 in 16.16 signed)
    writer.write_bits(32768, 18);
    // RotateSkew1 = -65536 (-1.0 in 16.16 signed) -> 18-bit signed integer representation: (-65536) & 0x3FFFF = 0x30000
    int32_t skew1_raw = -65536;
    uint32_t skew1_bits = static_cast<uint32_t>(skew1_raw) & ((1U << 18) - 1);
    writer.write_bits(skew1_bits, 18);

    // NTranslateBits = 12
    writer.write_bits(12, 5);
    // TranslateX = 400 twips (20.0 px)
    writer.write_bits(400, 12);
    // TranslateY = -200 twips (-10.0 px) -> 12-bit signed: (-200) & 0xFFF
    int32_t trans_y_raw = -200;
    uint32_t trans_y_bits = static_cast<uint32_t>(trans_y_raw) & ((1U << 12) - 1);
    writer.write_bits(trans_y_bits, 12);

    writer.align_byte();

    BitReader reader(writer.buffer.data(), writer.buffer.size());
    Matrix2D mat = read_swf_matrix(reader);

    std::cout << "  Decoded scale_x=" << mat.a << ", scale_y=" << mat.d
              << ", skew0=" << mat.b << ", skew1=" << mat.c
              << ", tx=" << mat.tx << ", ty=" << mat.ty << std::endl;

    assert(std::abs(mat.a - 2.0f) < 0.0001f);
    assert(std::abs(mat.d - 1.0f) < 0.0001f);
    assert(std::abs(mat.b - 0.5f) < 0.0001f);
    assert(std::abs(mat.c - (-1.0f)) < 0.0001f);
    assert(std::abs(mat.tx - 20.0f) < 0.0001f);
    assert(std::abs(mat.ty - (-10.0f)) < 0.0001f);

    std::cout << "  -> PASSED" << std::endl;
}

void test_per_frame_command_queue() {
    std::cout << "[Test 3] Per-Frame Command Queue Execution..." << std::endl;

    SWFParser parser;

    DisplayList& dl = parser.get_display_list();

    SWFShapeDefinition shape;
    shape.character_id = 7;
    dl.register_shape(shape);

    Matrix2D m0; m0.tx = 10.0f; m0.ty = 20.0f;
    dl.place_object_matrix(2, 7, m0, true);
    assert(dl.get_active_objects().find(2) != dl.get_active_objects().end());
    assert(dl.get_active_objects().at(2).character_id == 7);
    assert(dl.get_active_objects().at(2).matrix.tx == 10.0f);

    Matrix2D m1; m1.tx = 80.0f; m1.ty = 90.0f;
    dl.place_object_matrix(2, 0, m1, false);
    assert(dl.get_active_objects().find(2) != dl.get_active_objects().end());
    assert(dl.get_active_objects().at(2).character_id == 7);
    assert(dl.get_active_objects().at(2).matrix.tx == 80.0f);

    dl.remove_object(2);
    assert(dl.get_active_objects().find(2) == dl.get_active_objects().end());

    std::cout << "  -> PASSED" << std::endl;
}

int main() {
    std::cout << "=== Ruffle-Parity PlaceObject2 & Fixed-Point Matrix Test Suite ===" << std::endl;
    test_place_object_move_flag();
    test_fixed_point_matrix_decoding();
    test_per_frame_command_queue();
    std::cout << "ALL RUFFLE-PARITY TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
