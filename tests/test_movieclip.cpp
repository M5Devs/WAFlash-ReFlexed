#include "swf_parser.h"
#include "display_list.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <cmath>

int main() {
    printf("[TEST] Starting Phase B MovieClip & Scene Graph Unit Tests...\n");

    // Test 1: Matrix2D Affine Concatenation ($M_{world} = M_{parent} \times M_{child}$)
    {
        Matrix2D parent;
        parent.a = 2.0f; parent.b = 0.0f;
        parent.c = 0.0f; parent.d = 2.0f;
        parent.tx = 100.0f; parent.ty = 50.0f;

        Matrix2D child;
        child.a = 1.0f; child.b = 0.0f;
        child.c = 0.0f; child.d = 1.0f;
        child.tx = 10.0f; child.ty = 20.0f;

        Matrix2D world = Matrix2D::multiply(parent, child);
        // Tx_world = parent.a * child.tx + parent.c * child.ty + parent.tx = 2.0*10 + 0*20 + 100 = 120
        // Ty_world = parent.b * child.tx + parent.d * child.ty + parent.ty = 0*10 + 2.0*20 + 50 = 90
        assert(std::abs(world.tx - 120.0f) < 1e-4f);
        assert(std::abs(world.ty - 90.0f) < 1e-4f);
        assert(std::abs(world.a - 2.0f) < 1e-4f);
        assert(std::abs(world.d - 2.0f) < 1e-4f);
        printf("  [PASS] Test 1: Matrix2D concatenation calculated accurate affine world coordinates.\n");
    }

    // Test 2: MovieClipInstance Frame Advancing and Child Propagation
    {
        auto root = std::make_shared<MovieClipInstance>();
        root->total_frames = 10;
        root->current_frame = 1;

        auto child_clip = std::make_shared<MovieClipInstance>();
        child_clip->total_frames = 3;
        child_clip->current_frame = 1;

        root->add_child(1, child_clip);

        root->advance_frame();
        assert(root->current_frame == 2);
        assert(child_clip->current_frame == 2);

        root->advance_frame();
        assert(root->current_frame == 3);
        assert(child_clip->current_frame == 3);

        root->advance_frame();
        assert(root->current_frame == 4);
        assert(child_clip->current_frame == 1); // Looped back to 1

        child_clip->goto_and_stop(2);
        assert(child_clip->current_frame == 2);
        assert(child_clip->is_playing == false);

        root->advance_frame();
        assert(root->current_frame == 5);
        assert(child_clip->current_frame == 2); // Remained at 2 since stopped

        printf("  [PASS] Test 2: MovieClipInstance recursive frame advancing and state control verified.\n");
    }

    // Test 3: TagDefineSprite Ingestion into DisplayList Stage Tree
    {
        SWFParser parser;

        // Construct mock TagDefineSprite (Tag 39) with sub-tag TagPlaceObject2
        SWFSpriteDefinition sprite_def;
        sprite_def.sprite_id = 50;
        sprite_def.frame_count = 5;

        DisplayObject sub_obj;
        sub_obj.depth = 1;
        sub_obj.character_id = 100;
        sub_obj.transform_x = 15;
        sub_obj.transform_y = 25;
        sub_obj.matrix.tx = 15.0f;
        sub_obj.matrix.ty = 25.0f;
        sprite_def.sub_objects.push_back(sub_obj);

        parser.get_display_list().register_sprite(sprite_def);

        // Place sprite instance on stage at depth 2 with translation (50, 50)
        Matrix2D stage_mat;
        stage_mat.tx = 50.0f;
        stage_mat.ty = 50.0f;
        parser.get_display_list().place_object_matrix(2, 50, stage_mat);

        auto stage_root = parser.get_display_list().get_root_stage();
        assert(stage_root != nullptr);
        assert(stage_root->children.size() == 1);

        auto stage_child = stage_root->children[2];
        assert(stage_child != nullptr);
        assert(stage_child->get_type() == DisplayObjectType::MovieClip);

        auto mc_child = std::static_pointer_cast<MovieClipInstance>(stage_child);
        assert(mc_child->character_id == 50);
        assert(mc_child->total_frames == 5);
        assert(mc_child->children.size() == 1);

        auto leaf_shape = mc_child->children[1];
        assert(leaf_shape != nullptr);
        assert(leaf_shape->character_id == 100);

        Matrix2D leaf_world = Matrix2D::multiply(stage_root->local_matrix, Matrix2D::multiply(mc_child->local_matrix, leaf_shape->local_matrix));
        assert(std::abs(leaf_world.tx - 65.0f) < 1e-4f);
        assert(std::abs(leaf_world.ty - 75.0f) < 1e-4f);

        printf("  [PASS] Test 3: TagDefineSprite scene graph tree hierarchy successfully constructed and transformed.\n");
    }

    printf("[TEST] All Phase B MovieClip & Scene Graph unit tests passed successfully!\n");
    return 0;
}
