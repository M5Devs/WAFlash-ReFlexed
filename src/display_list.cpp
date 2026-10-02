#include "display_list.h"
#include <algorithm>
#include <cstring>

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

void DisplayList::export_state(SerializedDisplayListState& out_state) const {
    std::memset(&out_state, 0, sizeof(SerializedDisplayListState));
    uint32_t count = 0;
    for (const auto& pair : m_stage_objects) {
        if (count >= MAX_DISPLAY_OBJECTS) break;
        const DisplayObject& obj = pair.second;
        out_state.objects[count].depth = obj.depth;
        out_state.objects[count].character_id = obj.character_id;
        out_state.objects[count].transform_x = obj.transform_x;
        out_state.objects[count].transform_y = obj.transform_y;
        count++;
    }
    out_state.object_count = count;
}

void DisplayList::import_state(const SerializedDisplayListState& in_state) {
    m_stage_objects.clear();
    uint32_t count = std::min(in_state.object_count, static_cast<uint32_t>(MAX_DISPLAY_OBJECTS));
    for (uint32_t i = 0; i < count; ++i) {
        const SerializedDisplayObject& s_obj = in_state.objects[i];
        DisplayObject obj;
        obj.depth = s_obj.depth;
        obj.character_id = s_obj.character_id;
        obj.transform_x = s_obj.transform_x;
        obj.transform_y = s_obj.transform_y;
        m_stage_objects[obj.depth] = obj;
    }
}

const SWFShapeDefinition* DisplayList::find_shape(uint16_t character_id) const {
    auto it = m_dictionary.find(character_id);
    if (it != m_dictionary.end()) {
        return &it->second;
    }
    return nullptr;
}
