#include "vector_rasterizer.h"

void VectorTessellator::subdivide_quadratic_bezier(
    Point2D p0, Point2D p1, Point2D p2,
    std::vector<Point2D>& out_points,
    int max_depth
) {
    float dx = p2.x - p0.x;
    float dy = p2.y - p0.y;
    float len_sq = dx * dx + dy * dy;

    float dist_sq = 0.0f;
    if (len_sq > 1e-6f) {
        float num = std::abs(dy * p1.x - dx * p1.y + p2.x * p0.y - p2.y * p0.x);
        dist_sq = (num * num) / len_sq;
    } else {
        float dist_x = p1.x - p0.x;
        float dist_y = p1.y - p0.y;
        dist_sq = dist_x * dist_x + dist_y * dist_y;
    }

    if (dist_sq <= (FLATNESS_TOLERANCE * FLATNESS_TOLERANCE) || max_depth <= 0) {
        out_points.push_back(p2);
        return;
    }

    // De Casteljau midpoint split at t = 0.5
    Point2D p01  = { (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f };
    Point2D p12  = { (p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f };
    Point2D p012 = { (p01.x + p12.x) * 0.5f, (p01.y + p12.y) * 0.5f };

    // Subdivide left & right halves
    subdivide_quadratic_bezier(p0, p01, p012, out_points, max_depth - 1);
    subdivide_quadratic_bezier(p012, p12, p2, out_points, max_depth - 1);
}

void VectorTessellator::rasterize_polygon(
    const std::vector<Point2D>& vertices,
    uint32_t fill_color_xrgb,
    uint32_t* frame_buffer,
    uint32_t fb_width,
    uint32_t fb_height
) {
    if (vertices.size() < 3 || !frame_buffer || fb_width == 0 || fb_height == 0) return;

    float min_y = vertices[0].y, max_y = vertices[0].y;
    for (const auto& v : vertices) {
        min_y = std::min(min_y, v.y);
        max_y = std::max(max_y, v.y);
    }

    int scan_start = std::clamp(static_cast<int>(std::floor(min_y)), 0, static_cast<int>(fb_height) - 1);
    int scan_end   = std::clamp(static_cast<int>(std::ceil(max_y)), 0, static_cast<int>(fb_height) - 1);

    size_t num_verts = vertices.size();
    std::vector<float> node_x;

    for (int y = scan_start; y <= scan_end; ++y) {
        float scan_y = static_cast<float>(y) + 0.5f;
        node_x.clear();

        for (size_t i = 0; i < num_verts; ++i) {
            Point2D v1 = vertices[i];
            Point2D v2 = vertices[(i + 1) % num_verts];

            if ((v1.y < scan_y && v2.y >= scan_y) || (v2.y < scan_y && v1.y >= scan_y)) {
                if (std::abs(v2.y - v1.y) > 1e-6f) {
                    float x_intersect = v1.x + (scan_y - v1.y) / (v2.y - v1.y) * (v2.x - v1.x);
                    node_x.push_back(x_intersect);
                }
            }
        }

        std::sort(node_x.begin(), node_x.end());

        for (size_t i = 0; i + 1 < node_x.size(); i += 2) {
            if (node_x[i] >= node_x[i + 1]) continue;

            int x_start = std::clamp(static_cast<int>(std::ceil(node_x[i])), 0, static_cast<int>(fb_width) - 1);
            int x_end   = std::clamp(static_cast<int>(std::floor(node_x[i + 1])), 0, static_cast<int>(fb_width) - 1);

            for (int x = x_start; x <= x_end; ++x) {
                if (x >= 0 && x < static_cast<int>(fb_width) && y >= 0 && y < static_cast<int>(fb_height)) {
                    frame_buffer[y * fb_width + x] = fill_color_xrgb;
                }
            }
        }
    }
}
