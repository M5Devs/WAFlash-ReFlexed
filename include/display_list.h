#ifndef DISPLAY_LIST_H
#define DISPLAY_LIST_H

#include <cstdint>
#include <unordered_map>
#include <map>
#include "serialization.h"

struct SWFShapeDefinition {
    uint16_t character_id{0};
    int32_t x_min{0};
    int32_t x_max{0};
    int32_t y_min{0};
    int32_t y_max{0};
    uint32_t fill_color_xrgb{0x00FFFFFF};
};

struct DisplayObject {
    uint16_t depth{0};
    uint16_t character_id{0};
    int32_t transform_x{0};
    int32_t transform_y{0};
};

class DisplayList {
public:
    DisplayList() = default;
    ~DisplayList() = default;

    void register_shape(const SWFShapeDefinition& shape);
    void place_object(uint16_t depth, uint16_t character_id, int32_t x, int32_t y);
    void remove_object(uint16_t depth);
    void clear();

    void export_state(SerializedDisplayListState& out_state) const;
    void import_state(const SerializedDisplayListState& in_state);

    const SWFShapeDefinition* find_shape(uint16_t character_id) const;
    const std::map<uint16_t, DisplayObject>& get_active_objects() const { return m_stage_objects; }
    const std::unordered_map<uint16_t, SWFShapeDefinition>& get_dictionary() const { return m_dictionary; }

private:
    std::unordered_map<uint16_t, SWFShapeDefinition> m_dictionary;
    std::map<uint16_t, DisplayObject> m_stage_objects;
};

#endif // DISPLAY_LIST_H
