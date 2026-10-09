#include "display_list.h"
#include <algorithm>
#include <cstring>

DisplayList::DisplayList() {
    m_root_stage = std::make_shared<MovieClipInstance>();
    m_root_stage->depth = 0;
    m_root_stage->character_id = 0;
    m_root_stage->name = "stage";
}

void DisplayList::register_shape(const SWFShapeDefinition& shape) {
    m_dictionary[shape.character_id] = shape;
}

void DisplayList::register_sprite(const SWFSpriteDefinition& sprite) {
    m_sprite_dictionary[sprite.sprite_id] = sprite;
}

void DisplayList::place_object(uint16_t depth, uint16_t character_id, int32_t x, int32_t y, bool has_character) {
    Matrix2D mat;
    mat.tx = static_cast<float>(x);
    mat.ty = static_cast<float>(y);

    auto existing_it = m_stage_objects.find(depth);
    if (!has_character && existing_it != m_stage_objects.end()) {
        mat = existing_it->second.matrix;
        mat.tx = static_cast<float>(x);
        mat.ty = static_cast<float>(y);
    }

    place_object_matrix(depth, character_id, mat, has_character);
}

void DisplayList::place_object_matrix(uint16_t depth, uint16_t character_id, const Matrix2D& mat, bool has_character) {
    uint16_t effective_char_id = character_id;

    auto existing_it = m_stage_objects.find(depth);
    if (!has_character && existing_it != m_stage_objects.end()) {
        effective_char_id = existing_it->second.character_id;
    }

    DisplayObject obj;
    obj.depth = depth;
    obj.character_id = effective_char_id;
    obj.transform_x = static_cast<int32_t>(mat.tx);
    obj.transform_y = static_cast<int32_t>(mat.ty);
    obj.matrix = mat;
    m_stage_objects[depth] = obj;

    if (!has_character && m_root_stage) {
        auto node_it = m_root_stage->children.find(depth);
        if (node_it != m_root_stage->children.end() && node_it->second) {
            node_it->second->local_matrix = mat;
            return;
        }
    }

    if (effective_char_id != 0 && m_root_stage) {
        auto sprite_it = m_sprite_dictionary.find(effective_char_id);
        if (sprite_it != m_sprite_dictionary.end()) {
            auto clip = create_movieclip_from_sprite(effective_char_id);
            clip->local_matrix = mat;
            m_root_stage->add_child(depth, clip);
        } else {
            auto shape_node = std::make_shared<ShapeInstance>();
            shape_node->character_id = effective_char_id;
            shape_node->local_matrix = mat;
            m_root_stage->add_child(depth, shape_node);
        }
    }
}

std::shared_ptr<MovieClipInstance> DisplayList::create_movieclip_from_sprite(uint16_t sprite_id) {
    auto clip = std::make_shared<MovieClipInstance>();
    clip->character_id = sprite_id;

    auto sprite_it = m_sprite_dictionary.find(sprite_id);
    if (sprite_it != m_sprite_dictionary.end()) {
        const auto& sprite = sprite_it->second;
        clip->total_frames = sprite.frame_count > 0 ? sprite.frame_count : 1;

        for (const auto& sub_obj : sprite.sub_objects) {
            auto sub_sprite_it = m_sprite_dictionary.find(sub_obj.character_id);
            if (sub_sprite_it != m_sprite_dictionary.end()) {
                auto child_clip = create_movieclip_from_sprite(sub_obj.character_id);
                child_clip->local_matrix = sub_obj.matrix;
                child_clip->local_matrix.tx = static_cast<float>(sub_obj.transform_x);
                child_clip->local_matrix.ty = static_cast<float>(sub_obj.transform_y);
                clip->add_child(sub_obj.depth, child_clip);
            } else {
                auto child_shape = std::make_shared<ShapeInstance>();
                child_shape->character_id = sub_obj.character_id;
                child_shape->local_matrix = sub_obj.matrix;
                child_shape->local_matrix.tx = static_cast<float>(sub_obj.transform_x);
                child_shape->local_matrix.ty = static_cast<float>(sub_obj.transform_y);
                clip->add_child(sub_obj.depth, child_shape);
            }
        }
    }
    return clip;
}

void DisplayList::update_object_matrix(uint16_t depth, const Matrix2D& mat) {
    place_object_matrix(depth, 0, mat, false);
}

void DisplayList::remove_object(uint16_t depth) {
    m_stage_objects.erase(depth);
    if (m_root_stage) {
        m_root_stage->remove_child(depth);
    }
}

void DisplayList::clear_active_objects() {
    m_stage_objects.clear();
    if (m_root_stage) {
        m_root_stage->children.clear();
    }
}

void DisplayList::clear() {
    m_dictionary.clear();
    m_sprite_dictionary.clear();
    clear_active_objects();
}

void DisplayList::advance_frame() {
    if (m_root_stage) {
        m_root_stage->advance_frame();
    }
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
    clear();
    uint32_t count = std::min(in_state.object_count, static_cast<uint32_t>(MAX_DISPLAY_OBJECTS));
    for (uint32_t i = 0; i < count; ++i) {
        const SerializedDisplayObject& s_obj = in_state.objects[i];
        place_object(s_obj.depth, s_obj.character_id, s_obj.transform_x, s_obj.transform_y);
    }
}

const SWFShapeDefinition* DisplayList::find_shape(uint16_t character_id) const {
    auto it = m_dictionary.find(character_id);
    if (it != m_dictionary.end()) {
        return &it->second;
    }
    return nullptr;
}

const SWFSpriteDefinition* DisplayList::find_sprite(uint16_t character_id) const {
    auto it = m_sprite_dictionary.find(character_id);
    if (it != m_sprite_dictionary.end()) {
        return &it->second;
    }
    return nullptr;
}
