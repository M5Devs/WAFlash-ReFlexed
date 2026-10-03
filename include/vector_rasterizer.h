#ifndef VECTOR_RASTERIZER_H
#define VECTOR_RASTERIZER_H

#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>

struct Point2D {
    float x{0.0f};
    float y{0.0f};
};

class VectorTessellator {
public:
    static constexpr float FLATNESS_TOLERANCE = 0.25f; // Pixel tolerance

    static void subdivide_quadratic_bezier(
        Point2D p0, Point2D p1, Point2D p2,
        std::vector<Point2D>& out_points,
        int max_depth = 8
    );

    static void rasterize_polygon(
        const std::vector<Point2D>& vertices,
        uint32_t fill_color_xrgb,
        uint32_t* frame_buffer,
        uint32_t fb_width,
        uint32_t fb_height
    );
};

#endif // VECTOR_RASTERIZER_H
