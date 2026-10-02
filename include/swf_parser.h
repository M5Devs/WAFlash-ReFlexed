#ifndef SWF_PARSER_H
#define SWF_PARSER_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include "display_list.h"

struct SWFHeader {
    char signature[3];
    uint8_t version;
    uint32_t file_length;
    uint32_t width_px;
    uint32_t height_px;
    float frame_rate;
    uint16_t frame_count;
    uint32_t background_color_xrgb;
};

struct ABCTag {
    uint32_t flags;
    std::string name;
    std::vector<uint8_t> bytecode_data;
};

class SWFParser {
public:
    SWFParser();
    ~SWFParser();

    bool parse(const uint8_t* data, size_t size);

    const SWFHeader& get_header() const { return m_header; }
    const std::vector<ABCTag>& get_abc_tags() const { return m_abc_tags; }
    const std::vector<size_t>& get_show_frame_positions() const { return m_show_frame_positions; }
    size_t get_show_frame_count() const { return m_show_frame_positions.size(); }
    const DisplayList& get_display_list() const { return m_display_list; }
    DisplayList& get_display_list() { return m_display_list; }

private:
    SWFHeader m_header;
    std::vector<ABCTag> m_abc_tags;
    std::vector<size_t> m_show_frame_positions;
    DisplayList m_display_list;
};

#endif // SWF_PARSER_H
