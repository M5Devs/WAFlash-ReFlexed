#include "retro_flash_memory.h"
#include "libretro.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    printf("[TEST] Starting RetroAchievements Memory Map Unit Test...\n");

    // 1. Verify structure layout & member offsets
    static_assert(offsetof(RetroFlashMemoryMap, swf_hash) == 0x0000, "swf_hash offset must be 0x0000");
    static_assert(offsetof(RetroFlashMemoryMap, player_score) == 0x0010, "player_score offset must be 0x0010");
    static_assert(offsetof(RetroFlashMemoryMap, player_lives) == 0x0014, "player_lives offset must be 0x0014");
    static_assert(offsetof(RetroFlashMemoryMap, player_hp) == 0x0018, "player_hp offset must be 0x0018");
    static_assert(offsetof(RetroFlashMemoryMap, stage_id) == 0x001C, "stage_id offset must be 0x001C");
    static_assert(offsetof(RetroFlashMemoryMap, flags) == 0x0020, "flags offset must be 0x0020");

    assert(offsetof(RetroFlashMemoryMap, player_score) == 0x0010);
    assert(offsetof(RetroFlashMemoryMap, player_hp) == 0x0018);
    printf("  [PASS] Field offset assertions validated (player_score=0x0010, player_hp=0x0018).\n");

    // 2. Initialize Libretro core stub
    retro_init();

    // 3. Verify retro_get_memory_data returns valid pointer and correct size
    void* mem_ptr = retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    size_t mem_size = retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);

    assert(mem_ptr != nullptr);
    assert(mem_size == sizeof(RetroFlashMemoryMap));
    assert(mem_size == 0x10000);
    printf("  [PASS] retro_get_memory_data returned valid non-null buffer (%zu bytes).\n", mem_size);

    // 4. Test writing mock values (e.g., HP = 100, score = 5000)
    RetroFlashMemoryMap* mem_map = static_cast<RetroFlashMemoryMap*>(mem_ptr);
    mem_map->player_hp = 100;
    mem_map->player_score = 5000;
    mem_map->player_lives = 3;
    mem_map->stage_id = 2;
    mem_map->flags = 0x00000001;

    // Verify raw memory inspection at exact byte offsets
    uint8_t* raw_bytes = static_cast<uint8_t*>(mem_ptr);
    uint32_t hp_from_raw = *reinterpret_cast<uint32_t*>(raw_bytes + 0x0018);
    uint32_t score_from_raw = *reinterpret_cast<uint32_t*>(raw_bytes + 0x0010);

    (void)hp_from_raw;
    (void)score_from_raw;

    assert(hp_from_raw == 100);
    assert(score_from_raw == 5000);
    assert(mem_map->player_hp == 100);
    assert(mem_map->player_score == 5000);
    printf("  [PASS] Mock variable writes (HP=100, Score=5000) correctly reflected in memory map.\n");

    retro_deinit();
    printf("[TEST] All RetroAchievements Memory Map tests passed successfully!\n");
    return 0;
}
