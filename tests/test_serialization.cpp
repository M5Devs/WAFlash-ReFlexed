#include "libretro.h"
#include "retro_flash_memory.h"
#include "serialization.h"
#include "swf_parser.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

static void mock_video_cb(const void* data, unsigned width, unsigned height, size_t pitch) {
    (void)data; (void)width; (void)height; (void)pitch;
}
static void mock_audio_cb(int16_t left, int16_t right) { (void)left; (void)right; }
static size_t mock_audio_batch_cb(const int16_t* data, size_t frames) { (void)data; return frames; }
static void mock_input_poll_cb(void) {}
static int16_t mock_input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
    (void)port; (void)device; (void)index; (void)id;
    return 0;
}

int main() {
    printf("[TEST] Starting Deterministic Save State Serialization Unit Test...\n");

    // 1. Initialize core callbacks & subsystem
    retro_set_video_refresh(mock_video_cb);
    retro_set_audio_sample(mock_audio_cb);
    retro_set_audio_sample_batch(mock_audio_batch_cb);
    retro_set_input_poll(mock_input_poll_cb);
    retro_set_input_state(mock_input_state_cb);
    retro_init();

    // 2. Setup initial core state
    size_t state_size = retro_serialize_size();
    assert(state_size == sizeof(CoreStatePayload));
    printf("  [PASS] retro_serialize_size() returned expected state payload size (%zu bytes).\n", state_size);

    std::vector<uint8_t> buffer(state_size, 0);

    // Populate RetroFlashMemoryMap via system RAM pointer
    RetroFlashMemoryMap* mem_map = static_cast<RetroFlashMemoryMap*>(retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    assert(mem_map != nullptr);

    mem_map->player_score = 9999;
    mem_map->player_hp = 75;
    mem_map->player_lives = 5;
    mem_map->stage_id = 42;
    mem_map->flags = 0xABCD1234;
    std::memset(mem_map->swf_hash, 0xAB, sizeof(mem_map->swf_hash));

    // Serialize current state into buffer
    bool serialized = retro_serialize(buffer.data(), buffer.size());
    assert(serialized == true);
    printf("  [PASS] retro_serialize succeeded.\n");

    // Check payload header
    const CoreStatePayload* saved_payload = reinterpret_cast<const CoreStatePayload*>(buffer.data());
    assert(saved_payload->header.magic == WAFLASH_SERIALIZE_MAGIC);
    assert(saved_payload->header.version == WAFLASH_SERIALIZE_VERSION);
    assert(saved_payload->memory_map.player_score == 9999);
    assert(saved_payload->memory_map.player_hp == 75);

    // 3. Mutate core state completely
    mem_map->player_score = 0;
    mem_map->player_hp = 0;
    mem_map->player_lives = 0;
    mem_map->stage_id = 0;
    mem_map->flags = 0;
    std::memset(mem_map->swf_hash, 0, sizeof(mem_map->swf_hash));

    assert(mem_map->player_score == 0);
    assert(mem_map->player_hp == 0);

    // 4. Unserialize state back from buffer
    bool unserialized = retro_unserialize(buffer.data(), buffer.size());
    assert(unserialized == true);
    printf("  [PASS] retro_unserialize succeeded.\n");

    // 5. Assert 100% bitwise fidelity after restoration
    assert(mem_map->player_score == 9999);
    assert(mem_map->player_hp == 75);
    assert(mem_map->player_lives == 5);
    assert(mem_map->stage_id == 42);
    assert(mem_map->flags == 0xABCD1234);
    for (size_t i = 0; i < sizeof(mem_map->swf_hash); ++i) {
        assert(mem_map->swf_hash[i] == 0xAB);
    }
    printf("  [PASS] Roundtrip serialization restored all state fields with 100%% bitwise fidelity.\n");

    // 6. Test serialization edge cases (buffer too small or invalid magic header)
    std::vector<uint8_t> small_buffer(state_size - 1, 0);
    assert(retro_serialize(small_buffer.data(), small_buffer.size()) == false);
    assert(retro_unserialize(small_buffer.data(), small_buffer.size()) == false);

    CoreStatePayload invalid_payload = *saved_payload;
    invalid_payload.header.magic = 0xDEADBEEF;
    assert(retro_unserialize(&invalid_payload, sizeof(invalid_payload)) == false);
    printf("  [PASS] Edge cases (small buffer, bad magic header) correctly rejected.\n");

    retro_deinit();
    printf("[TEST] All Deterministic Save State Serialization unit tests passed successfully!\n");
    return 0;
}
