#include "abc_parser.h"
#include <cstring>

ABCParser::ABCParser() {}
ABCParser::~ABCParser() {}

bool ABCParser::read_u30(const uint8_t* data, size_t size, size_t& offset, uint32_t& value) {
    value = 0;
    uint32_t shift = 0;
    for (int i = 0; i < 5; ++i) {
        if (offset >= size) return false;
        uint8_t b = data[offset++];
        value |= (static_cast<uint32_t>(b & 0x7F) << shift);
        if ((b & 0x80) == 0) return true;
        shift += 7;
    }
    return true;
}

bool ABCParser::read_s32(const uint8_t* data, size_t size, size_t& offset, int32_t& value) {
    uint32_t uval = 0;
    if (!read_u30(data, size, offset, uval)) return false;
    value = static_cast<int32_t>(uval);
    return true;
}

bool ABCParser::parse(const uint8_t* data, size_t size, ABCFile& out_abc) {
    if (!data || size < 4) return false;

    out_abc = ABCFile();
    size_t offset = 0;

    // Minor version (uint16_le)
    out_abc.minor_version = static_cast<uint16_t>(data[offset]) |
                           (static_cast<uint16_t>(data[offset + 1]) << 8);
    offset += 2;

    // Major version (uint16_le)
    out_abc.major_version = static_cast<uint16_t>(data[offset]) |
                           (static_cast<uint16_t>(data[offset + 1]) << 8);
    offset += 2;

    // AVM2 version check: major version must be 46
    if (out_abc.major_version != 46) {
        return false;
    }

    // Parse Constant Pool (cpool_info)
    // 1. Integers
    uint32_t int_count = 0;
    if (!read_u30(data, size, offset, int_count)) return false;
    if (int_count > 0) {
        out_abc.constant_integers.push_back(0); // Index 0 is default
        for (uint32_t i = 1; i < int_count; ++i) {
            int32_t val = 0;
            if (!read_s32(data, size, offset, val)) return false;
            out_abc.constant_integers.push_back(val);
        }
    }

    // 2. Uints
    uint32_t uint_count = 0;
    if (!read_u30(data, size, offset, uint_count)) return false;
    if (uint_count > 0) {
        out_abc.constant_uints.push_back(0); // Index 0 is default
        for (uint32_t i = 1; i < uint_count; ++i) {
            uint32_t val = 0;
            if (!read_u30(data, size, offset, val)) return false;
            out_abc.constant_uints.push_back(val);
        }
    }

    // 3. Doubles
    uint32_t double_count = 0;
    if (!read_u30(data, size, offset, double_count)) return false;
    if (double_count > 0) {
        out_abc.constant_doubles.push_back(0.0); // Index 0 is default
        for (uint32_t i = 1; i < double_count; ++i) {
            if (offset + 8 > size) return false;
            double val = 0.0;
            std::memcpy(&val, data + offset, sizeof(double));
            offset += 8;
            out_abc.constant_doubles.push_back(val);
        }
    }

    // 4. Strings
    uint32_t string_count = 0;
    if (!read_u30(data, size, offset, string_count)) return false;
    if (string_count > 0) {
        out_abc.constant_strings.push_back(""); // Index 0 is default
        for (uint32_t i = 1; i < string_count; ++i) {
            uint32_t str_len = 0;
            if (!read_u30(data, size, offset, str_len)) return false;
            if (offset + str_len > size) return false;
            std::string str(reinterpret_cast<const char*>(data + offset), str_len);
            offset += str_len;
            out_abc.constant_strings.push_back(str);
        }
    }

    return true;
}
