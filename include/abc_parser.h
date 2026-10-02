#ifndef ABC_PARSER_H
#define ABC_PARSER_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

struct ABCFile {
    uint16_t minor_version = 0;
    uint16_t major_version = 0;
    std::vector<int32_t> constant_integers;
    std::vector<uint32_t> constant_uints;
    std::vector<double> constant_doubles;
    std::vector<std::string> constant_strings;
};

class ABCParser {
public:
    ABCParser();
    ~ABCParser();

    static bool read_u30(const uint8_t* data, size_t size, size_t& offset, uint32_t& value);
    static bool read_s32(const uint8_t* data, size_t size, size_t& offset, int32_t& value);

    bool parse(const uint8_t* data, size_t size, ABCFile& out_abc);
};

#endif // ABC_PARSER_H
