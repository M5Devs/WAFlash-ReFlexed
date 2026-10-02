#ifndef RETRO_FLASH_MEMORY_H
#define RETRO_FLASH_MEMORY_H

#include <cstdint>
#include <cstddef>

#pragma pack(push, 1)
struct RetroFlashMemoryMap {
    uint8_t  swf_hash[16];       // 0x0000 - 0x000F
    uint32_t player_score;      // 0x0010 - 0x0013
    uint32_t player_lives;      // 0x0014 - 0x0017
    uint32_t player_hp;         // 0x0018 - 0x001B
    uint32_t stage_id;          // 0x001C - 0x001F
    uint32_t flags;             // 0x0020 - 0x0023
    uint8_t  scratchpad[0xFFDC];// Remainder up to 64KB (0x10000 total size)
};
#pragma pack(pop)

static_assert(sizeof(RetroFlashMemoryMap) == 0x10000, "RetroFlashMemoryMap size must be exactly 64KB (0x10000)");
static_assert(offsetof(RetroFlashMemoryMap, player_score) == 0x0010, "player_score offset must be 0x0010");
static_assert(offsetof(RetroFlashMemoryMap, player_hp) == 0x0018, "player_hp offset must be 0x0018");

#endif // RETRO_FLASH_MEMORY_H
