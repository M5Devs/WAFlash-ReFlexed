#include "wasm_api.h"
#include "base/tu_file.h"
#include "gameswf/gameswf.h"
#include "gameswf/gameswf_root.h"
#include "gameswf/gameswf_player.h"
#include "gameswf/gameswf_movie_def.h"
#include "gameswf/gameswf_types.h"
#include "audio_mixer.h"
#include <vector>
#include <cstring>
#include <algorithm>

extern gameswf::render_handler* create_render_handler_wasm();
extern tu_file* create_gameswf_tu_file_mem(const uint8_t* data, size_t size);
namespace gameswf { void ensure_loaders_registered(); }

std::vector<uint32_t> g_wasm_framebuffer;
uint32_t              g_wasm_width  = 800;
uint32_t              g_wasm_height = 600;

static gameswf::player* g_player = nullptr;
static gameswf::root*   g_movie  = nullptr;
static AudioMixer       g_audio_mixer;

struct WasmSoundHandler : public gameswf::sound_handler {
    struct SoundData {
        std::vector<int16_t> samples;
        int sample_rate = 44100;
        bool stereo = true;
        bool is_stream = false;
    };

    std::vector<SoundData> m_sounds;

    void reset() {
        m_sounds.clear();
    }

    virtual bool is_open() override {
        return true;
    }

    virtual int create_sound(
        void* data,
        int data_bytes,
        int /*sample_count*/,
        format_type /*format*/,
        int sample_rate,
        bool stereo) override
    {
        int id = static_cast<int>(m_sounds.size());
        m_sounds.push_back(SoundData());
        m_sounds[id].sample_rate = sample_rate;
        m_sounds[id].stereo = stereo;

        if (data && data_bytes > 0) {
            m_sounds[id].is_stream = false;
            size_t num_samples = data_bytes / sizeof(int16_t);
            const int16_t* pcm = static_cast<const int16_t*>(data);
            m_sounds[id].samples.assign(pcm, pcm + num_samples);
        } else {
            m_sounds[id].is_stream = true;
        }
        return id;
    }

    virtual int load_sound(const char* /*url*/) override {
        return -1;
    }

    virtual void append_sound(int sound_handle, void* data, int data_bytes) override {
        if (sound_handle >= 0 && sound_handle < static_cast<int>(m_sounds.size())) {
            if (data && data_bytes > 0) {
                size_t num_samples = data_bytes / sizeof(int16_t);
                const int16_t* pcm = static_cast<const int16_t*>(data);
                m_sounds[sound_handle].samples.insert(
                    m_sounds[sound_handle].samples.end(), pcm, pcm + num_samples);
                g_audio_mixer.queue_samples(pcm, num_samples);
            }
        }
    }

    virtual void play_sound(gameswf::as_object* /*listener_obj*/, int sound_handle, int loop_count) override {
        if (sound_handle >= 0 && sound_handle < static_cast<int>(m_sounds.size())) {
            const auto& snd = m_sounds[sound_handle];
            if (!snd.is_stream && !snd.samples.empty()) {
                int loops = (loop_count < 0) ? 1 : (loop_count + 1);
                for (int l = 0; l < loops; ++l) {
                    g_audio_mixer.queue_samples(snd.samples.data(), snd.samples.size());
                }
            }
        }
    }

    virtual void set_volume(int /*sound_handle*/, int /*volume*/) override {}
    virtual void set_max_volume(int /*vol*/) override {}
    virtual void stop_sound(int /*sound_handle*/) override {}
    virtual void stop_all_sounds() override {}
    virtual void delete_sound(int /*sound_handle*/) override {}
};

static WasmSoundHandler g_sound_handler;

extern "C" {

int wasm_load_swf(const uint8_t* data, size_t size) {
    if (!data || size == 0) return 0;

    if (!g_player) {
        g_player = new gameswf::player();
        g_player->set_separate_thread(false);
    }

    gameswf::set_render_handler(create_render_handler_wasm());
    g_audio_mixer.reset();
    g_sound_handler.reset();
    gameswf::set_sound_handler(&g_sound_handler);

    tu_file* in = create_gameswf_tu_file_mem(data, size);
    if (!in) return 0;

    gameswf::ensure_loaders_registered();

    gameswf::gc_ptr<gameswf::movie_def_impl> def = new gameswf::movie_def_impl(g_player, gameswf::DO_LOAD_BITMAPS, gameswf::DO_LOAD_FONT_SHAPES);
    def->read(in); // Note: movie_def_impl::read_tags() deletes in (m_origin_in)

    g_movie = def->create_instance();
    if (!g_movie) return 0;

    int w = g_movie->get_movie_width();
    int h = g_movie->get_movie_height();
    g_wasm_width  = (w > 0) ? static_cast<uint32_t>(w) : 800;
    g_wasm_height = (h > 0) ? static_cast<uint32_t>(h) : 600;

    g_wasm_framebuffer.assign(g_wasm_width * g_wasm_height, 0xFFFFFFFF);

    return 1;
}

void wasm_step_frame(void) {
    if (!g_movie) return;

    float fps = g_movie->get_frame_rate();
    float delta_time = (fps > 0.0f) ? (1.0f / fps) : (1.0f / 30.0f);

    g_movie->advance(delta_time);
    g_movie->display();
}

const uint32_t* wasm_get_framebuffer(void) {
    return g_wasm_framebuffer.data();
}

int wasm_get_width(void)  { return static_cast<int>(g_wasm_width);  }
int wasm_get_height(void) { return static_cast<int>(g_wasm_height); }

void wasm_send_key(int keycode, int is_down) {
    if (g_movie && g_player) {
        g_movie->notify_key_event(g_player, static_cast<gameswf::key::code>(keycode), is_down != 0);
    }
}

void wasm_send_pointer(int x, int y, int is_down) {
    if (g_movie) {
        g_movie->notify_mouse_state(x, y, is_down ? 1 : 0);
    }
}

uint32_t wasm_get_player_score(void) { return 0; }
uint32_t wasm_get_player_hp(void)    { return 0; }

size_t wasm_get_audio_samples(int16_t* out_buffer, size_t num_frames) {
    if (!out_buffer || num_frames == 0) return 0;
    return g_audio_mixer.render_frame(out_buffer, num_frames);
}

} // extern "C"
