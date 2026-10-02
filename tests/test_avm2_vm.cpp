#include "avm2_vm.h"
#include "retro_flash_memory.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

static void write_u30(std::vector<uint8_t>& buf, uint32_t val) {
    do {
        uint8_t b = val & 0x7F;
        val >>= 7;
        if (val != 0) {
            b |= 0x80;
        }
        buf.push_back(b);
    } while (val != 0);
}

int main() {
    printf("[TEST] Starting AVM2 Bytecode Execution Engine Unit Tests...\n");

    // Test 1: Stack push/pop and arithmetic operations (10 + 25 = 35)
    {
        AVM2VM vm;
        std::vector<uint8_t> bytecode;
        // pushbyte 10 (0x24 0x0A)
        bytecode.push_back(0x24);
        bytecode.push_back(10);
        // pushbyte 25 (0x24 0x19)
        bytecode.push_back(0x24);
        bytecode.push_back(25);
        // add (0xA0)
        bytecode.push_back(0xA0);

        bool res = vm.execute(bytecode.data(), bytecode.size());
        assert(res == true);
        assert(vm.get_stack().size() == 1);
        assert(vm.top().as_int() == 35);
        printf("  [PASS] Test 1: Stack push/pop and arithmetic operations (10 + 25 = 35).\n");
    }

    // Test 2: Local register read/write (setlocal_1, getlocal_1)
    {
        AVM2VM vm;
        std::vector<uint8_t> bytecode;
        // pushbyte 42
        bytecode.push_back(0x24);
        bytecode.push_back(42);
        // setlocal_1 (0xD5)
        bytecode.push_back(0xD5);
        // pushbyte 100
        bytecode.push_back(0x24);
        bytecode.push_back(100);
        // getlocal_1 (0xD1)
        bytecode.push_back(0xD1);
        // add
        bytecode.push_back(0xA0);

        bool res = vm.execute(bytecode.data(), bytecode.size());
        assert(res == true);
        assert(vm.get_local(1).as_int() == 42);
        assert(vm.top().as_int() == 142);
        printf("  [PASS] Test 2: Local register read/write (setlocal_1, getlocal_1).\n");
    }

    // Test 3: RetroAchievements memory synchronization verification
    {
        RetroFlashMemoryMap mem_map;
        std::memset(&mem_map, 0, sizeof(RetroFlashMemoryMap));

        AVM2VM vm;
        vm.set_retro_memory(&mem_map);

        std::vector<uint8_t> bytecode;
        // Score = 5000 -> pushshort 5000 -> setlocal 1
        bytecode.push_back(0x25);
        write_u30(bytecode, 5000);
        bytecode.push_back(0x63);
        write_u30(bytecode, 1);

        // HP = 100 -> pushbyte 100 -> setlocal 2
        bytecode.push_back(0x24);
        bytecode.push_back(100);
        bytecode.push_back(0x63);
        write_u30(bytecode, 2);

        // Lives = 3 -> pushbyte 3 -> setlocal 3
        bytecode.push_back(0x24);
        bytecode.push_back(3);
        bytecode.push_back(0x63);
        write_u30(bytecode, 3);

        // Stage = 2 -> pushbyte 2 -> setlocal 4
        bytecode.push_back(0x24);
        bytecode.push_back(2);
        bytecode.push_back(0x63);
        write_u30(bytecode, 4);

        bool res = vm.execute(bytecode.data(), bytecode.size());
        assert(res == true);

        // Verify memory map update
        assert(mem_map.player_score == 5000);
        assert(mem_map.player_hp == 100);
        assert(mem_map.player_lives == 3);
        assert(mem_map.stage_id == 2);

        // Direct raw pointer inspection
        uint8_t* raw_mem = reinterpret_cast<uint8_t*>(&mem_map);
        uint32_t score_raw = *reinterpret_cast<uint32_t*>(raw_mem + 0x0010);
        uint32_t hp_raw = *reinterpret_cast<uint32_t*>(raw_mem + 0x0018);

        assert(score_raw == 5000);
        assert(hp_raw == 100);

        printf("  [PASS] Test 3: RetroAchievements memory synchronization (Score=5000, HP=100) verified.\n");
    }

    printf("[TEST] All AVM2 VM tests passed successfully!\n");
    return 0;
}
