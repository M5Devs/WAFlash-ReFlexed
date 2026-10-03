# Ruffle (Rust) to C++ Architecture Blueprint for WAFlash-ReFlexed

## 1. Executive Summary & Architectural Overview

`WAFlash-ReFlexed` aims to deliver a high-performance, open-source C++17 Adobe Flash execution core targeting Libretro, WebAssembly (Emscripten), and native desktop platforms. While `WAFlash-ReFlexed` leverages Adobe's official `avmplus` engine for ActionScript 3 bytecode interpretation, accurate visual rendering, audio playback, and SWF timeline management require clean-room implementations inspired by the industry-standard Flash emulator **Ruffle** (`ruffle-rs/ruffle`).

This document provides a comprehensive reverse-engineering blueprint mapping Ruffle’s Rust subsystems—specifically **Audio Streaming & Synchronization**, **MovieClip & DisplayObject Hierarchy**, and **Vector Curve Tessellation & Rasterization**—into native C++17 architecture and class designs.

---

## 2. Rust-to-C++ Paradigm & Translation Guide

Ruffle relies heavily on Rust's memory safety primitives, garbage collection macros (`gc-arena`), zero-cost abstractions, tagged enums (`enum`), and trait interfaces. Below is the structural translation guide mapping Ruffle's Rust paradigms into high-performance, modern C++17 constructs.

### 2.1 Enums with Payloads & Pattern Matching
In Rust, `enum` variants can hold diverse data types, pattern-matched via `match`.

* **Rust Paradigm:**
```rust
pub enum DisplayObject<'gc> {
    MovieClip(GcCell<'gc, MovieClipData<'gc>>),
    Graphic(GcCell<'gc, GraphicData<'gc>>),
    MorphShape(GcCell<'gc, MorphShapeData<'gc>>),
    EditText(GcCell<'gc, EditTextData<'gc>>),
}
```

* **C++17 Mapping (`std::variant` & `std::visit`):**
```cpp
#include <variant>
#include <memory>

class MovieClipInstance;
class GraphicInstance;
class MorphShapeInstance;
class EditTextInstance;

using DisplayObjectVariant = std::variant<
    std::shared_ptr<MovieClipInstance>,
    std::shared_ptr<GraphicInstance>,
    std::shared_ptr<MorphShapeInstance>,
    std::shared_ptr<EditTextInstance>
>;

// Pattern matching dispatch in C++17:
std::visit([](auto&& obj) {
    obj->render();
}, display_object_variant);
```

### 2.2 Memory Management & Garbage Collection Handles
Ruffle uses `gc-arena` (`GcCell<'gc, T>`) for interior mutability and garbage collection of Flash display nodes and ActionScript objects.

* **Rust Paradigm:**
```rust
pub struct GcCell<'gc, T: 'gc>(pub &'gc RefCell<T>);
```

* **C++17 Mapping (Arena Allocator + Smart Pointers / Non-Owning Raw Handles):**
In `WAFlash-ReFlexed`, scene graph nodes are parent-owned or managed by a arena pool (`DisplayList`).
```cpp
// Explicit parent-child ownership or shared handle reference
using DisplayObjectHandle = std::shared_ptr<DisplayObjectNode>;
using WeakDisplayObjectHandle = std::weak_ptr<DisplayObjectNode>;
```

### 2.3 Trait Interfaces -> Abstract Polymorphic Interfaces
Rust traits translate directly to C++ pure virtual interface classes.

* **Rust Paradigm:**
```rust
pub trait RenderBackend {
    fn render_shape(&mut self, shape: &RenderShape, transform: &Transform);
    fn draw_rect(&mut self, rect: Rect, color: Color);
}
```

* **C++17 Mapping:**
```cpp
class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;
    virtual void render_shape(const RenderShape& shape, const Matrix2D& transform) = 0;
    virtual void draw_rect(const Rect& rect, uint32_t color_xrgb) = 0;
};
```

---

## 3. Subsystem 1: Audio Streaming & Synchronization Pipeline

### 3.1 Ruffle Architecture & SWF Specification
Flash SWF streams frame-synchronized audio across two primary tags:
1. **`TagSoundStreamHead` / `TagSoundStreamHead2` (Tag 45):** Defines global stream attributes: audio format (e.g. Uncompressed PCM, ADPCM, MP3), source sample rate (5.5kHz, 11kHz, 22kHz, 44.1kHz), sample size (8-bit or 16-bit), channels (Mono/Stereo), and requested samples per frame.
2. **`TagSoundStreamBlock` (Tag 19):** Interleaved directly before `ShowFrame` (Tag 1) tags. Contains raw compressed or uncompressed audio bytes intended for playback during that timeline frame.

#### MP3 Latency Compensation & Frame Synchronization
In MP3-encoded SWF streams (`SoundFormat = 2`), MP3 encoders introduce initial delay frames (usually 1152 samples or 576 samples of encoder delay padding). `TagSoundStreamHead2` contains a `LatencySeek` field specifying the sample offset skip required before playing back decoded PCM samples.

```
+-----------------------------------------------------------------------------------+
| SWF Timeline Stream Parsing Pipeline                                             |
+-----------------------------------------------------------------------------------+
|                                                                                   |
|  [ TagSoundStreamHead2 (Tag 45) ]                                                 |
|    |-> Format: MP3 (2), Rate: 44.1kHz, Stereo: True, SeekOffset: 1152 samples    |
|                                                                                   |
|  [ TagSoundStreamBlock (Tag 19) ] (Frame 1)                                       |
|    |-> MP3 Bitstream Chunk ---> [ minimp3 Decoder ]                               |
|                                     |                                             |
|                                     v                                             |
|                             [ PCM Buffer ]                                        |
|                                     |                                             |
|                                     v                                             |
|                         [ Latency Compensation ] (Skip first 1152 samples)        |
|                                     |                                             |
|                                     v                                             |
|                           [ Sample Rate Converter ] (Resample to 44.1kHz stereo)  |
|                                     |                                             |
|                                     v                                             |
|                         [ AudioMixer Ring Buffer ]                                |
|                                                                                   |
+-----------------------------------------------------------------------------------+
```

### 3.2 Concrete C++ Class Architecture & Implementation Design

We expand `include/audio_mixer.h` and `src/audio_mixer.cpp` with a single-header MP3 decoder integration (`minimp3`) and stream resampler.

#### C++ Header Blueprint: `include/audio_stream_decoder.h`
```cpp
#ifndef AUDIO_STREAM_DECODER_H
#define AUDIO_STREAM_DECODER_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>

enum class SoundFormat : uint8_t {
    UncompressedNativeLE = 0,
    ADPCM = 1,
    MP3 = 2,
    UncompressedBE = 3,
    Nellymoser = 6
};

struct SoundStreamHeader {
    SoundFormat format{SoundFormat::MP3};
    uint32_t sample_rate{44100};
    bool is_16bit{true};
    bool is_stereo{true};
    uint16_t samples_per_frame{0};
    int16_t latency_seek{0};
    bool active{false};
};

class AudioStreamDecoder {
public:
    AudioStreamDecoder() = default;
    ~AudioStreamDecoder() = default;

    void initialize(const SoundStreamHeader& header);

    // Decodes incoming TagSoundStreamBlock payload into 44.1kHz stereo int16_t PCM
    std::vector<int16_t> decode_block(const uint8_t* compressed_data, size_t data_size);

    void reset();

private:
    SoundStreamHeader m_header{};
    bool m_latency_compensated{false};

    // Internal minimp3 state decoder handle
    void* m_mp3_decoder_state{nullptr};

    // Resampling helper: Linear interpolation from native sample_rate to 44100Hz
    std::vector<int16_t> resample_to_44k_stereo(const int16_t* pcm_in, size_t in_samples, uint32_t in_rate, bool is_stereo);
};

#endif // AUDIO_STREAM_DECODER_H
```

#### C++ Integration in `src/audio_mixer.cpp`
```cpp
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "audio_stream_decoder.h"
#include "audio_mixer.h"

void AudioStreamDecoder::initialize(const SoundStreamHeader& header) {
    m_header = header;
    m_latency_compensated = false;
    if (m_header.format == SoundFormat::MP3) {
        if (!m_mp3_decoder_state) {
            mp3dec_t* dec = new mp3dec_t();
            mp3dec_init(dec);
            m_mp3_decoder_state = reinterpret_cast<void*>(dec);
        }
    }
}

std::vector<int16_t> AudioStreamDecoder::decode_block(const uint8_t* compressed_data, size_t data_size) {
    std::vector<int16_t> decoded_pcm;
    if (!compressed_data || data_size == 0) return decoded_pcm;

    if (m_header.format == SoundFormat::MP3) {
        mp3dec_t* dec = reinterpret_cast<mp3dec_t*>(m_mp3_decoder_state);
        mp3dec_frame_info_t info;
        int16_t pcm_buf[MINIMP3_MAX_SAMPLES_PER_FRAME];

        size_t bytes_left = data_size;
        const uint8_t* stream_ptr = compressed_data;

        // Skip 2-byte MP3 sample seek header present in SWF MP3 streams
        if (bytes_left >= 2) {
            int16_t initial_samples_seek = static_cast<int16_t>(stream_ptr[0] | (stream_ptr[1] << 8));
            stream_ptr += 2;
            bytes_left -= 2;
        }

        while (bytes_left > 0) {
            int samples = mp3dec_decode_frame(dec, stream_ptr, static_cast<int>(bytes_left), pcm_buf, &info);
            if (info.frame_bytes == 0) break;

            stream_ptr += info.frame_bytes;
            bytes_left -= info.frame_bytes;

            if (samples > 0) {
                size_t total_samples = samples * info.channels;

                // Resample to global output target (44100Hz Stereo)
                auto resampled = resample_to_44k_stereo(pcm_buf, total_samples, info.hz, info.channels == 2);
                decoded_pcm.insert(decoded_pcm.end(), resampled.begin(), resampled.end());
            }
        }

        // Apply Latency Compensation on initial frame
        if (!m_latency_compensated && m_header.latency_seek > 0) {
            size_t skip_samples = m_header.latency_seek * AudioMixer::CHANNELS;
            if (decoded_pcm.size() > skip_samples) {
                decoded_pcm.erase(decoded_pcm.begin(), decoded_pcm.begin() + skip_samples);
            } else {
                decoded_pcm.clear();
            }
            m_latency_compensated = true;
        }
    } else if (m_header.format == SoundFormat::UncompressedNativeLE) {
        // Raw PCM decoding & sample format expansion
        size_t sample_count = data_size / (m_header.is_16bit ? 2 : 1);
        const int16_t* raw_samples = reinterpret_cast<const int16_t*>(compressed_data);
        auto resampled = resample_to_44k_stereo(raw_samples, sample_count, m_header.sample_rate, m_header.is_stereo);
        decoded_pcm.insert(decoded_pcm.end(), resampled.begin(), resampled.end());
    }

    return decoded_pcm;
}
```

---

## 4. Subsystem 2: MovieClip & DisplayObject Hierarchy

### 4.1 Ruffle Architecture & Sub-Timeline Management
Flash Flash MovieClips (`TagDefineSprite`, Tag 39) are self-contained sub-timelines with independent frame numbers, child display lists, matrix transforms, and ActionScript control contexts (`gotoAndPlay`, `stop`, `play`).

#### Parent-Child Transformation Matrix Concatenation
Each node in the DisplayObject tree maintains a 2x3 affine transformation matrix ($M_{local}$). The absolute transformation matrix on stage ($M_{world}$) is recursively calculated by pre-multiplying parent transformations:

$$M_{world} = M_{parent} \times M_{local}$$

Where the 2x3 affine matrix represents:

$$\begin{bmatrix} a & c & tx \\ b & d & ty \end{bmatrix}$$

```
+-----------------------------------------------------------------------------------+
| DisplayObject Scene Graph & Hierarchy                                             |
+-----------------------------------------------------------------------------------+
|                                                                                   |
|                           [ Stage Root MovieClip ]                                |
|                           M_world = Identity                                      |
|                                     |                                             |
|          +--------------------------+--------------------------+                  |
|          | (Depth 1)                                           | (Depth 2)        |
|          v                                                     v                  |
|    [ SWFShape ]                                    [ Sub-MovieClip (Tag 39) ]     |
|    CharacterID: 5                                  CharacterID: 10                |
|    M_world = M_shape                               M_world = M_parent * M_clip    |
|                                                                |                  |
|                                                                v                  |
|                                                    [ Nested Inner Shape ]         |
|                                                    CharacterID: 12                |
|                                                    M_world = M_clip * M_inner     |
+-----------------------------------------------------------------------------------+
```

### 4.2 C++ Architecture & Class Design Blueprint

We extend `include/display_list.h` and `src/display_list.cpp` to introduce a hierarchical `DisplayObjectNode` base class and `MovieClipInstance` tree manager.

#### C++ Header Blueprint: `include/display_list_hierarchy.h`
```cpp
#ifndef DISPLAY_LIST_HIERARCHY_H
#define DISPLAY_LIST_HIERARCHY_H

#include <cstdint>
#include <vector>
#include <map>
#include <memory>
#include <string>

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
    virtual void render(class IRenderer* renderer, const Matrix2D& parent_matrix) = 0;
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
        current_frame = std::clamp(frame, (uint16_t)1, total_frames);
        is_playing = true;
    }

    void goto_and_stop(uint16_t frame) {
        current_frame = std::clamp(frame, (uint16_t)1, total_frames);
        is_playing = false;
    }

    void add_child(uint16_t depth, std::shared_ptr<DisplayObjectNode> child) {
        child->depth = depth;
        child->parent = this;
        children[depth] = child;
    }

    void remove_child(uint16_t depth) {
        children.erase(depth);
    }

    void advance_frame() override {
        if (is_playing) {
            current_frame++;
            if (current_frame > total_frames) {
                current_frame = 1; // Loop sub-timeline
            }
        }
        // Recursively advance children
        for (auto& [depth, child] : children) {
            child->advance_frame();
        }
    }

    void render(class IRenderer* renderer, const Matrix2D& parent_matrix) override {
        Matrix2D world = Matrix2D::multiply(parent_matrix, local_matrix);
        // Render depth-ordered children (from back to front)
        for (auto& [depth, child] : children) {
            child->render(renderer, world);
        }
    }
};

#endif // DISPLAY_LIST_HIERARCHY_H
```

---

## 5. Subsystem 3: Vector Path Ingestion & Tessellation

### 5.1 Ruffle Architecture & Curve Subdivision Algorithm
SWF shapes (`TagDefineShape1-4`) represent geometry using straight lines (`StraightEdgeRecord`) and quadratic Bezier curves (`CurvedEdgeRecord`).

#### Quadratic Bezier Representation
A quadratic Bezier curve is defined by 3 points:
- $P_0 = (x_0, y_0)$: Start Anchor Point
- $P_1 = (cx, cy)$: Control Point
- $P_2 = (x_1, y_1)$: End Anchor Point

The parametric formulation $P(t)$ for $t \in [0, 1]$ is:

$$P(t) = (1-t)^2 P_0 + 2(1-t)t P_1 + t^2 P_2$$

#### Adaptive De Casteljau Curve Subdivision
Rather than fixed-step evaluation, Ruffle uses adaptive curve subdivision based on flatness tolerances (maximum distance error $d_{max}$ from the control point $P_1$ to the chord $\overline{P_0 P_2}$).

```
                  P1 (Control Point)
                     /\
                    /  \
                   /    \
                  /  d   \
                 /   v    \
  P0 (Start) ---+----------+--- P2 (End)
```

Error distance $d$:

$$d = \frac{|(y_2 - y_0)cx - (x_2 - x_0)cy + x_2 y_0 - y_2 x_0|}{\sqrt{(y_2 - y_0)^2 + (x_2 - x_0)^2}}$$

If $d \le \text{tolerance}$ (e.g. $0.5$ pixels or twips), the curve is approximated by the straight line segment $\overline{P_0 P_2}$. Otherwise, the curve is split at $t = 0.5$ using De Casteljau's algorithm into two sub-curves and evaluated recursively.

### 5.2 C++ Rasterization & Curve Subdivision Implementation Design

We design a CPU software rasterizer and curve subdivider in `src/core_stub.cpp` and `src/wasm_api.cpp`.

#### C++ Implementation Blueprint: `src/vector_rasterizer.cpp`
```cpp
#include <vector>
#include <cmath>
#include <algorithm>

struct Point2D {
    float x;
    float y;
};

struct LinearSegment {
    Point2D p0;
    Point2D p1;
};

class VectorTessellator {
public:
    static constexpr float FLATNESS_TOLERANCE = 0.25f; // Pixel tolerance

    static void subdivide_quadratic_bezier(
        Point2D p0, Point2D p1, Point2D p2,
        std::vector<Point2D>& out_points,
        int max_depth = 8
    ) {
        // Distance from control point p1 to line (p0 -> p2)
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

    // Scanline polygon fill rasterizer for framebuffer output
    static void rasterize_polygon(
        const std::vector<Point2D>& vertices,
        uint32_t fill_color_xrgb,
        uint32_t* frame_buffer,
        uint32_t fb_width,
        uint32_t fb_height
    ) {
        if (vertices.size() < 3 || !frame_buffer) return;

        // Find bounding box
        float min_y = vertices[0].y, max_y = vertices[0].y;
        for (const auto& v : vertices) {
            min_y = std::min(min_y, v.y);
            max_y = std::max(max_y, v.y);
        }

        int scan_start = std::clamp(static_cast<int>(std::floor(min_y)), 0, static_cast<int>(fb_height) - 1);
        int scan_end   = std::clamp(static_cast<int>(std::ceil(max_y)), 0, static_cast<int>(fb_height) - 1);

        for (int y = scan_start; y <= scan_end; ++y) {
            float scan_y = static_cast<float>(y) + 0.5f;
            std::vector<float> node_x;

            size_t num_verts = vertices.size();
            for (size_t i = 0; i < num_verts; ++i) {
                Point2D v1 = vertices[i];
                Point2D v2 = vertices[(i + 1) % num_verts];

                if ((v1.y < scan_y && v2.y >= scan_y) || (v2.y < scan_y && v1.y >= scan_y)) {
                    float x_intersect = v1.x + (scan_y - v1.y) / (v2.y - v1.y) * (v2.x - v1.x);
                    node_x.push_back(x_intersect);
                }
            }

            std::sort(node_x.begin(), node_x.end());

            for (size_t i = 0; i + 1 < node_x.size(); i += 2) {
                int x_start = std::clamp(static_cast<int>(std::ceil(node_x[i])), 0, static_cast<int>(fb_width) - 1);
                int x_end   = std::clamp(static_cast<int>(std::floor(node_x[i + 1])), 0, static_cast<int>(fb_width) - 1);

                for (int x = x_start; x <= x_end; ++x) {
                    frame_buffer[y * fb_width + x] = fill_color_xrgb;
                }
            }
        }
    }
};
```

---

## 6. Sequential Implementation Plan & Roadmap

```
  Sprint Schedule & Feature Roadmap:

  +-----------------------------------------------------------------------------------+
  | Phase A: Audio Streaming & Decoders (Sprint 1)                                    |
  |  - Integrate single-header minimp3 into src/audio_mixer.cpp                      |
  |  - Implement TagSoundStreamHead2 parsing & sample rate resampler                   |
  |  - Add MP3 latency compensation filter for audio/video sync                       |
  +-----------------------------------------------------------------------------------+
                                           |
                                           v
  +-----------------------------------------------------------------------------------+
  | Phase B: MovieClip Hierarchy & Matrix Scene Graph (Sprint 2)                      |
  |  - Refactor include/display_list.h with MovieClipInstance & DisplayObjectNode     |
  |  - Implement parent-child 2x3 affine matrix composition                           |
  |  - Implement recursive timeline advance and frame boundary loops                  |
  +-----------------------------------------------------------------------------------+
                                           |
                                           v
  +-----------------------------------------------------------------------------------+
  | Phase C: Vector Curve Subdivision & Rasterizer (Sprint 3)                        |
  |  - Implement adaptive De Casteljau subdivision for SWF CurvedEdgeRecord           |
  |  - Integrate scanline fill algorithm in src/core_stub.cpp & src/wasm_api.cpp       |
  |  - Validate rendering output against Libretro video frame callbacks               |
  +-----------------------------------------------------------------------------------+
```

### Sprint 1: Audio Streaming & Decoder Integration
- Integrate `minimp3` into `src/audio_mixer.cpp`.
- Connect `TagSoundStreamHead` / `TagSoundStreamHead2` sample rates directly to `AudioMixer` ring buffers.
- Write unit tests in `tests/test_sound_stream.cpp` verifying decoded audio playback.

### Sprint 2: MovieClip Hierarchy & Matrix Transformations
- Extend `DisplayList` to handle nested `MovieClipInstance` sub-timelines.
- Implement recursive `advance_frame()` and `render()` passes across child objects.
- Validate matrix transforms in `tests/test_sprite.cpp` and `tests/test_display_list.cpp`.

### Sprint 3: Vector Curve Subdivision & Framebuffer Rasterization
- Integrate `VectorTessellator::subdivide_quadratic_bezier` into `src/swf_parser.cpp` and `src/core_stub.cpp`.
- Connect rasterized polygon output to `libretro` video output buffer `retro_run()`.
- Ensure zero-regression build passing all CTest test targets.
