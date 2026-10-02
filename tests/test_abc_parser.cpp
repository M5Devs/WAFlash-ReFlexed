#include "abc_parser.h"
#include <cassert>
#include <cstdio>
#include <vector>

static void write_u30(std::vector<uint8_t>& buf, uint32_t val) {
    do {
        uint8_t b = val & 0x7F;
        val >>= 7;
        if (val != 0) {
            b |= 0x80;
        }
        buf.push_back(b);
    } while (val != 0);
}

int main() {
    printf("[TEST] Starting ABC Bytecode Parser Unit Tests...\n");

    // Test 1: Helper read_u30
    {
        std::vector<uint8_t> buf;
        write_u30(buf, 0);
        write_u30(buf, 127);
        write_u30(buf, 128);
        write_u30(buf, 16383);
        write_u30(buf, 1000000);

        size_t offset = 0;
        uint32_t val = 0;

        assert(ABCParser::read_u30(buf.data(), buf.size(), offset, val) && val == 0);
        assert(ABCParser::read_u30(buf.data(), buf.size(), offset, val) && val == 127);
        assert(ABCParser::read_u30(buf.data(), buf.size(), offset, val) && val == 128);
        assert(ABCParser::read_u30(buf.data(), buf.size(), offset, val) && val == 16383);
        assert(ABCParser::read_u30(buf.data(), buf.size(), offset, val) && val == 1000000);
        printf("  [PASS] Test 1: Variable-length u30 decoding passed.\n");
    }

    // Test 2: Valid ABC File Header (16 / 46) and Constant Pool
    {
        std::vector<uint8_t> abc_buf;
        // Minor version = 16 (0x0010 LE)
        abc_buf.push_back(16);
        abc_buf.push_back(0);
        // Major version = 46 (0x002E LE)
        abc_buf.push_back(46);
        abc_buf.push_back(0);

        // Constant Pool:
        // int_count = 3 -> 2 entries: 42, -100
        write_u30(abc_buf, 3);
        write_u30(abc_buf, 42);
        write_u30(abc_buf, static_cast<uint32_t>(-100));

        // uint_count = 2 -> 1 entry: 3000000000U
        write_u30(abc_buf, 2);
        write_u30(abc_buf, 3000000000U);

        // double_count = 1 -> 0 entries
        write_u30(abc_buf, 1);

        // string_count = 3 -> 2 entries: "Hello", "World"
        write_u30(abc_buf, 3);
        std::string s1 = "Hello";
        write_u30(abc_buf, static_cast<uint32_t>(s1.size()));
        abc_buf.insert(abc_buf.end(), s1.begin(), s1.end());

        std::string s2 = "World";
        write_u30(abc_buf, static_cast<uint32_t>(s2.size()));
        abc_buf.insert(abc_buf.end(), s2.begin(), s2.end());

        ABCParser parser;
        ABCFile abc_file;
        bool res = parser.parse(abc_buf.data(), abc_buf.size(), abc_file);
        assert(res == true);
        assert(abc_file.minor_version == 16);
        assert(abc_file.major_version == 46);

        assert(abc_file.constant_integers.size() == 3);
        assert(abc_file.constant_integers[1] == 42);
        assert(abc_file.constant_integers[2] == -100);

        assert(abc_file.constant_uints.size() == 2);
        assert(abc_file.constant_uints[1] == 3000000000U);

        assert(abc_file.constant_strings.size() == 3);
        assert(abc_file.constant_strings[1] == "Hello");
        assert(abc_file.constant_strings[2] == "World");

        printf("  [PASS] Test 2: ABC Header and Constant Pool parsed correctly.\n");
    }

    // Test 3: Invalid ABC Major Version should fail
    {
        std::vector<uint8_t> bad_buf;
        bad_buf.push_back(16);
        bad_buf.push_back(0);
        bad_buf.push_back(99); // Invalid major version 99
        bad_buf.push_back(0);

        ABCParser parser;
        ABCFile abc_file;
        bool res = parser.parse(bad_buf.data(), bad_buf.size(), abc_file);
        assert(res == false);
        printf("  [PASS] Test 3: Invalid ABC major version correctly rejected.\n");
    }

    printf("[TEST] All ABC Parser tests passed successfully!\n");
    return 0;
}
