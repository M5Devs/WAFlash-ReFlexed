#include "display_list.h"

void DisplayList::register_shape(const SWFShapeDefinition& shape) {
    m_dictionary[shape.character_id] = shape;
}

void DisplayList::place_object(uint16_t depth, uint16_t character_id, int32_t x, int32_t y) {
    DisplayObject obj;
    obj.depth = depth;
    obj.character_id = character_id;
    obj.transform_x = x;
    obj.transform_y = y;
    m_stage_objects[depth] = obj;
}

void DisplayList::remove_object(uint16_t depth) {
    m_stage_objects.erase(depth);
}

void DisplayList::clear() {
    m_dictionary.clear();
    m_stage_objects.clear();
}

const SWFShapeDefinition* DisplayList::find_shape(uint16_t character_id) const {
    auto it = m_dictionary.find(character_id);
    if (it != m_dictionary.end()) {
        return &it->second;
    }
    return nullptr;
}
