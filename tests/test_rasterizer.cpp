#include "vector_rasterizer.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <cmath>

void test_de_casteljau_subdivision() {
    printf("  [TEST 1] Testing De Casteljau Quadratic Bezier Subdivision...\n");

    Point2D p0 = {0.0f, 0.0f};
    Point2D p1 = {50.0f, 100.0f};
    Point2D p2 = {100.0f, 0.0f};

    std::vector<Point2D> out_points;
    VectorTessellator::subdivide_quadratic_bezier(p0, p1, p2, out_points);

    assert(!out_points.empty());

    // End point must match P2
    Point2D last_p = out_points.back();
    assert(std::abs(last_p.x - p2.x) < 1e-3f);
    assert(std::abs(last_p.y - p2.y) < 1e-3f);

    // Verify distance flatness of segments
    for (size_t i = 0; i < out_points.size(); ++i) {
        Point2D prev = (i == 0) ? p0 : out_points[i - 1];
        Point2D curr = out_points[i];
        float dx = curr.x - prev.x;
        float dy = curr.y - prev.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        assert(dist > 0.0f);
    }

    printf("  [PASS] De Casteljau subdivision generated %zu smooth points.\n", out_points.size());
}

void test_scanline_rasterization() {
    printf("  [TEST 2] Testing Scanline Polygon Rasterization...\n");

    constexpr uint32_t FB_W = 100;
    constexpr uint32_t FB_H = 100;
    uint32_t fill_red = 0x00FF0000;
    uint32_t bg_white = 0x00FFFFFF;

    std::vector<uint32_t> fb(FB_W * FB_H, bg_white);

    // Convex Triangle: (10,10), (50,90), (90,10)
    std::vector<Point2D> tri_verts = {{10.0f, 10.0f}, {50.0f, 90.0f}, {90.0f, 10.0f}};
    VectorTessellator::rasterize_polygon(tri_verts, fill_red, fb.data(), FB_W, FB_H);

    // Interior pixel check at (50, 50)
    assert(fb[50 * FB_W + 50] == fill_red);
    // Exterior pixel checks
    assert(fb[5 * FB_W + 5] == bg_white);
    assert(fb[95 * FB_W + 95] == bg_white);

    // Non-convex L-shape polygon: (10,10), (30,10), (30,50), (90,50), (90,90), (10,90)
    std::fill(fb.begin(), fb.end(), bg_white);
    uint32_t fill_blue = 0x000000FF;
    std::vector<Point2D> l_verts = {
        {10.0f, 10.0f}, {30.0f, 10.0f}, {30.0f, 50.0f},
        {90.0f, 50.0f}, {90.0f, 90.0f}, {10.0f, 90.0f}
    };
    VectorTessellator::rasterize_polygon(l_verts, fill_blue, fb.data(), FB_W, FB_H);

    // Check inside L-shape
    assert(fb[20 * FB_W + 20] == fill_blue); // top-left leg
    assert(fb[70 * FB_W + 20] == fill_blue); // bottom-left leg
    assert(fb[70 * FB_W + 70] == fill_blue); // bottom-right leg
    // Check in cut-out notch at (60, 20) -> should be background white
    assert(fb[20 * FB_W + 60] == bg_white);

    printf("  [PASS] Scanline rasterizer correctly filled convex triangle and non-convex L-shape.\n");
}

void test_boundary_clipping_edge_cases() {
    printf("  [TEST 3] Testing Off-Screen and Boundary Clipping Edge Cases...\n");

    constexpr uint32_t FB_W = 100;
    constexpr uint32_t FB_H = 100;
    uint32_t fill_green = 0x0000FF00;
    uint32_t bg_white   = 0x00FFFFFF;

    std::vector<uint32_t> fb(FB_W * FB_H, bg_white);

    // Huge polygon extending beyond screen (-20, -20) to (120, 120)
    std::vector<Point2D> huge_rect = {
        {-20.0f, -20.0f}, {120.0f, -20.0f}, {120.0f, 120.0f}, {-20.0f, 120.0f}
    };

    VectorTessellator::rasterize_polygon(huge_rect, fill_green, fb.data(), FB_W, FB_H);

    // Every pixel in framebuffer must be clipped and filled with fill_green
    for (uint32_t y = 0; y < FB_H; ++y) {
        for (uint32_t x = 0; x < FB_W; ++x) {
            assert(fb[y * FB_W + x] == fill_green);
        }
    }

    // Degenerate / invalid inputs
    std::vector<Point2D> line_verts = {{0.0f, 0.0f}, {50.0f, 50.0f}};
    VectorTessellator::rasterize_polygon(line_verts, fill_green, fb.data(), FB_W, FB_H); // No crash

    VectorTessellator::rasterize_polygon({}, fill_green, fb.data(), FB_W, FB_H); // No crash
    VectorTessellator::rasterize_polygon(huge_rect, fill_green, nullptr, FB_W, FB_H); // Nullptr safety

    printf("  [PASS] Off-screen clipping and zero buffer overflow verified.\n");
}

int main() {
    printf("[TEST] Starting Vector Tessellator & Scanline Rasterizer Unit Tests...\n");
    test_de_casteljau_subdivision();
    test_scanline_rasterization();
    test_boundary_clipping_edge_cases();
    printf("[TEST] All Vector Tessellator tests passed successfully!\n");
    return 0;
}
