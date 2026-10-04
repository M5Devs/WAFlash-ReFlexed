#include "gameswf/gameswf.h"
#include "gameswf/gameswf_render.h"
#include "gameswf/gameswf_types.h"
#include "vector_rasterizer.h"
#include <vector>
#include <algorithm>
#include <cstdint>

extern std::vector<uint32_t> g_wasm_framebuffer;
extern uint32_t              g_wasm_width;
extern uint32_t              g_wasm_height;

struct dummy_bitmap_info : public gameswf::bitmap_info {
    int get_width() const override { return 1; }
    int get_height() const override { return 1; }
};

class glyph_provider_wasm : public gameswf::glyph_provider {
    gameswf::gc_ptr<gameswf::bitmap_info> m_stub_bi;
public:
    glyph_provider_wasm() {
        m_stub_bi = new dummy_bitmap_info();
    }
    ~glyph_provider_wasm() override {}

    gameswf::bitmap_info* get_char_image(gameswf::character_def* shape_glyph, Uint16 code,
                                          const tu_string& fontname, bool is_bold, bool is_italic,
                                          int fontsize, gameswf::rect* bounds, float* advance) override {
        (void)shape_glyph; (void)code; (void)fontname; (void)is_bold; (void)is_italic;
        if (bounds) {
            bounds->m_x_min = 0.0f;
            bounds->m_x_max = 0.0f;
            bounds->m_y_min = 0.0f;
            bounds->m_y_max = 0.0f;
        }
        if (advance) {
            float em_size = static_cast<float>(fontsize);
            if (em_size <= 0.0f) em_size = 1024.0f;
            *advance = em_size * 0.6f;
        }
        return m_stub_bi.get_ptr();
    }
};

class render_handler_wasm : public gameswf::render_handler {
    gameswf::matrix m_current_matrix;
    gameswf::cxform m_current_cxform;
    gameswf::rgba   m_current_color;

public:
    render_handler_wasm() {}
    ~render_handler_wasm() override {}

    gameswf::bitmap_info* create_bitmap_info_empty() override {
        return new dummy_bitmap_info();
    }
    gameswf::bitmap_info* create_bitmap_info_alpha(int w, int h, unsigned char* data) override {
        (void)w; (void)h; (void)data;
        return new dummy_bitmap_info();
    }
    gameswf::bitmap_info* create_bitmap_info_rgb(image::rgb* im) override {
        (void)im;
        return new dummy_bitmap_info();
    }
    gameswf::bitmap_info* create_bitmap_info_rgba(image::rgba* im) override {
        (void)im;
        return new dummy_bitmap_info();
    }
    gameswf::video_handler* create_video_handler() override {
        return nullptr;
    }

    void begin_display(gameswf::rgba background_color,
                       int viewport_x0, int viewport_y0,
                       int viewport_width, int viewport_height,
                       float x0, float x1, float y0, float y1) override {
        (void)viewport_x0; (void)viewport_y0; (void)viewport_width; (void)viewport_height;
        (void)x0; (void)x1; (void)y0; (void)y1;
        uint32_t bg_xrgb = (background_color.m_r << 16) | (background_color.m_g << 8) | background_color.m_b;
        if (!g_wasm_framebuffer.empty()) {
            std::fill(g_wasm_framebuffer.begin(), g_wasm_framebuffer.end(), bg_xrgb);
        }
    }

    void end_display() override {}

    void set_matrix(const gameswf::matrix& m) override {
        m_current_matrix = m;
    }

    void set_cxform(const gameswf::cxform& cx) override {
        m_current_cxform = cx;
    }

    void fill_style_disable(int fill_side) override { (void)fill_side; }

    void fill_style_color(int fill_side, const gameswf::rgba& color) override {
        (void)fill_side;
        m_current_color = color;
    }

    void fill_style_bitmap(int fill_side, gameswf::bitmap_info* bi, const gameswf::matrix& m,
                           bitmap_wrap_mode wm, bitmap_blend_mode bm) override {
        (void)fill_side; (void)bi; (void)m; (void)wm; (void)bm;
    }

    void line_style_disable() override {}
    void line_style_color(gameswf::rgba color) override { (void)color; }
    void line_style_width(float width) override { (void)width; }

    void draw_mesh_strip(const void* coords, int vertex_count) override {
        if (!coords || vertex_count < 3 || g_wasm_framebuffer.empty()) return;
        const Sint16* p = static_cast<const Sint16*>(coords);

        gameswf::rgba tf_color = m_current_cxform.transform(m_current_color);
        uint32_t color_xrgb = (tf_color.m_r << 16) | (tf_color.m_g << 8) | tf_color.m_b;

        std::vector<Point2D> pts(vertex_count);
        for (int i = 0; i < vertex_count; ++i) {
            float lx = static_cast<float>(p[i * 2]);
            float ly = static_cast<float>(p[i * 2 + 1]);
            gameswf::point pt_in(lx, ly);
            gameswf::point pt_out;
            m_current_matrix.transform(&pt_out, pt_in);
            pts[i] = { TWIPS_TO_PIXELS(pt_out.m_x), TWIPS_TO_PIXELS(pt_out.m_y) };
        }

        for (int i = 0; i < vertex_count - 2; ++i) {
            std::vector<Point2D> tri = { pts[i], pts[i + 1], pts[i + 2] };
            VectorTessellator::rasterize_polygon(tri, color_xrgb, g_wasm_framebuffer.data(), g_wasm_width, g_wasm_height);
        }
    }

    void draw_triangle_list(const void* coords, int vertex_count) override {
        if (!coords || vertex_count < 3 || g_wasm_framebuffer.empty()) return;
        const Sint16* p = static_cast<const Sint16*>(coords);

        gameswf::rgba tf_color = m_current_cxform.transform(m_current_color);
        uint32_t color_xrgb = (tf_color.m_r << 16) | (tf_color.m_g << 8) | tf_color.m_b;

        for (int i = 0; i + 2 < vertex_count; i += 3) {
            std::vector<Point2D> tri(3);
            for (int k = 0; k < 3; ++k) {
                float lx = static_cast<float>(p[(i + k) * 2]);
                float ly = static_cast<float>(p[(i + k) * 2 + 1]);
                gameswf::point pt_in(lx, ly);
                gameswf::point pt_out;
                m_current_matrix.transform(&pt_out, pt_in);
                tri[k] = { TWIPS_TO_PIXELS(pt_out.m_x), TWIPS_TO_PIXELS(pt_out.m_y) };
            }
            VectorTessellator::rasterize_polygon(tri, color_xrgb, g_wasm_framebuffer.data(), g_wasm_width, g_wasm_height);
        }
    }

    void draw_line_strip(const void* coords, int vertex_count) override { (void)coords; (void)vertex_count; }

    void draw_bitmap(const gameswf::matrix& m, gameswf::bitmap_info* bi,
                     const gameswf::rect& coords, const gameswf::rect& uv_coords, gameswf::rgba color) override {
        (void)m; (void)bi; (void)coords; (void)uv_coords; (void)color;
    }

    void set_antialiased(bool enable) override { (void)enable; }
    bool test_stencil_buffer(const gameswf::rect& bound, Uint8 pattern) override { (void)bound; (void)pattern; return false; }
    void begin_submit_mask() override {}
    void end_submit_mask() override {}
    void disable_mask() override {}
    bool is_visible(const gameswf::rect& bound) override { (void)bound; return true; }
    void open() override {}
};

gameswf::render_handler* create_render_handler_wasm() {
    gameswf::set_glyph_provider(new glyph_provider_wasm());
    return new render_handler_wasm();
}
