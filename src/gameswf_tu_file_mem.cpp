#include "base/tu_file.h"
#include <cstring>
#include <cstdint>

struct MemFileState {
    const uint8_t* data;
    size_t size;
    size_t pos;
};

static int mem_read_func(void* dst, int bytes, void* appdata) {
    auto* s = static_cast<MemFileState*>(appdata);
    if (!s || s->pos >= s->size) return 0;
    size_t avail = s->size - s->pos;
    size_t to_read = (static_cast<size_t>(bytes) < avail) ? static_cast<size_t>(bytes) : avail;
    std::memcpy(dst, s->data + s->pos, to_read);
    s->pos += to_read;
    return static_cast<int>(to_read);
}

static int mem_write_func(const void* src, int bytes, void* appdata) {
    return 0;
}

static int mem_seek_func(int pos, void* appdata) {
    auto* s = static_cast<MemFileState*>(appdata);
    if (!s) return TU_FILE_SEEK_ERROR;
    if (pos < 0 || static_cast<size_t>(pos) > s->size) return TU_FILE_SEEK_ERROR;
    s->pos = static_cast<size_t>(pos);
    return 0;
}

static int mem_seek_to_end_func(void* appdata) {
    auto* s = static_cast<MemFileState*>(appdata);
    if (!s) return TU_FILE_SEEK_ERROR;
    s->pos = s->size;
    return 0;
}

static int mem_tell_func(const void* appdata) {
    auto* s = static_cast<const MemFileState*>(appdata);
    return s ? static_cast<int>(s->pos) : -1;
}

static bool mem_get_eof_func(void* appdata) {
    auto* s = static_cast<const MemFileState*>(appdata);
    return s ? (s->pos >= s->size) : true;
}

static int mem_close_func(void* appdata) {
    delete static_cast<MemFileState*>(appdata);
    return 0;
}

tu_file* create_gameswf_tu_file_mem(const uint8_t* data, size_t size) {
    auto* state = new MemFileState{data, size, 0};
    return new tu_file(state, mem_read_func, mem_write_func, mem_seek_func, mem_seek_to_end_func, mem_tell_func, mem_get_eof_func, mem_close_func);
}
