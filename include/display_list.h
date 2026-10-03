#ifndef DISPLAY_LIST_H
#define DISPLAY_LIST_H

#include <cstdint>
#include <unordered_map>
#include <map>
#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include "serialization.h"
#include "vector_rasterizer.h"

struct Matrix2D {
    float a{1.0f}, b{0.0f};
    float c{0.0f}, d{1.0f};
    float tx{0.0f}, ty{0.0f};

    static Matrix2D multiply(const Matrix2D& parent, const Matrix2D& child) {
        Matrix2D res;
        res.a  = parent.a * child.a + parent.c * child.b;
        res.b  = parent.b * child.a + parent.d * child.b;
        res.c  = parent.a * child.c + parent.c * child.d;
        res.d  = parent.b * child.c + parent.d * child.d;
        res.tx = parent.a * child.tx + parent.c * child.ty + parent.tx;
        res.ty = parent.b * child.tx + parent.d * child.ty + parent.ty;
        return res;
    }
};

enum class DisplayObjectType {
    Shape,
    MovieClip,
    Text,
    MorphShape
};

struct SWFShapeDefinition {
    uint16_t character_id{0};
    int32_t x_min{0};
    int32_t x_max{0};
    int32_t y_min{0};
    int32_t y_max{0};
    uint32_t fill_color_xrgb{0x00FFFFFF};
    std::vector<Point2D> polygon_vertices;
};

struct DisplayObject {
    uint16_t depth{0};
    uint16_t character_id{0};
    int32_t transform_x{0};
    int32_t transform_y{0};
    Matrix2D matrix{};
};

struct SWFSpriteDefinition {
    uint16_t sprite_id{0};
    uint16_t frame_count{0};
    std::vector<DisplayObject> sub_objects;
};

class MovieClipInstance;

class DisplayObjectNode {
public:
    uint16_t depth{0};
    uint16_t character_id{0};
    std::string name;
    Matrix2D local_matrix{};
    MovieClipInstance* parent{nullptr};

    virtual ~DisplayObjectNode() = default;
    virtual DisplayObjectType get_type() const = 0;
    virtual void advance_frame() {}
};

class ShapeInstance : public DisplayObjectNode {
public:
    DisplayObjectType get_type() const override { return DisplayObjectType::Shape; }
};

class MovieClipInstance : public DisplayObjectNode, public std::enable_shared_from_this<MovieClipInstance> {
public:
    uint16_t total_frames{1};
    uint16_t current_frame{1};
    bool is_playing{true};

    // Depth-sorted child display objects
    std::map<uint16_t, std::shared_ptr<DisplayObjectNode>> children;

    DisplayObjectType get_type() const override { return DisplayObjectType::MovieClip; }

    void goto_and_play(uint16_t frame) {
        current_frame = std::clamp(frame, static_cast<uint16_t>(1), std::max(static_cast<uint16_t>(1), total_frames));
        is_playing = true;
    }

    void goto_and_stop(uint16_t frame) {
        current_frame = std::clamp(frame, static_cast<uint16_t>(1), std::max(static_cast<uint16_t>(1), total_frames));
        is_playing = false;
    }

    void add_child(uint16_t child_depth, std::shared_ptr<DisplayObjectNode> child) {
        if (!child) return;
        child->depth = child_depth;
        child->parent = this;
        children[child_depth] = child;
    }

    void remove_child(uint16_t child_depth) {
        children.erase(child_depth);
    }

    void advance_frame() override {
        if (is_playing && total_frames > 1) {
            current_frame++;
            if (current_frame > total_frames) {
                current_frame = 1; // Loop sub-timeline
            }
        }
        // Recursively advance children
        for (auto& [depth_val, child] : children) {
            if (child) {
                child->advance_frame();
            }
        }
    }
};

class DisplayList {
public:
    DisplayList();
    ~DisplayList() = default;

    void register_shape(const SWFShapeDefinition& shape);
    void register_sprite(const SWFSpriteDefinition& sprite);
    void place_object(uint16_t depth, uint16_t character_id, int32_t x, int32_t y);
    void place_object_matrix(uint16_t depth, uint16_t character_id, const Matrix2D& mat);
    void remove_object(uint16_t depth);
    void clear();

    void advance_frame();

    void export_state(SerializedDisplayListState& out_state) const;
    void import_state(const SerializedDisplayListState& in_state);

    const SWFShapeDefinition* find_shape(uint16_t character_id) const;
    const SWFSpriteDefinition* find_sprite(uint16_t character_id) const;
    const std::map<uint16_t, DisplayObject>& get_active_objects() const { return m_stage_objects; }
    const std::unordered_map<uint16_t, SWFShapeDefinition>& get_dictionary() const { return m_dictionary; }
    const std::unordered_map<uint16_t, SWFSpriteDefinition>& get_sprite_dictionary() const { return m_sprite_dictionary; }

    std::shared_ptr<MovieClipInstance> get_root_stage() { return m_root_stage; }
    std::shared_ptr<const MovieClipInstance> get_root_stage() const { return m_root_stage; }

    std::shared_ptr<MovieClipInstance> create_movieclip_from_sprite(uint16_t sprite_id);

private:
    std::unordered_map<uint16_t, SWFShapeDefinition> m_dictionary;
    std::unordered_map<uint16_t, SWFSpriteDefinition> m_sprite_dictionary;
    std::map<uint16_t, DisplayObject> m_stage_objects;
    std::shared_ptr<MovieClipInstance> m_root_stage;
};

#endif // DISPLAY_LIST_H
