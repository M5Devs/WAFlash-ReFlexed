#ifndef SERIALIZATION_H
#define SERIALIZATION_H

#include <cstdint>
#include <cstddef>
#include "retro_flash_memory.h"

#define WAFLASH_SERIALIZE_MAGIC   0x5741464C // 'WAFL'
#define WAFLASH_SERIALIZE_VERSION 1

#define MAX_AVM2_STACK_SIZE  256
#define MAX_AVM2_LOCALS_SIZE 256
#define MAX_DISPLAY_OBJECTS  512

#pragma pack(push, 1)

struct CoreStateHeader {
    uint32_t magic;           // 0x5741464C ('WAFL')
    uint32_t version;         // Format version (1)
    uint32_t current_frame;   // Active timeline frame index
    uint32_t reserved;        // Padding / reserved for alignment
};

enum class SerializedValueType : uint8_t {
    Undefined = 0,
    Null = 1,
    Boolean = 2,
    Integer = 3,
    Number = 4,
    String = 5
};

struct SerializedAVM2Value {
    SerializedValueType type;
    union {
        bool bool_val;
        int32_t int_val;
        double num_val;
    };
    char str_val[64];
};

struct SerializedAVM2State {
    uint32_t stack_count;
    SerializedAVM2Value stack[MAX_AVM2_STACK_SIZE];
    uint32_t locals_count;
    SerializedAVM2Value locals[MAX_AVM2_LOCALS_SIZE];
};

struct SerializedDisplayObject {
    uint16_t depth;
    uint16_t character_id;
    int32_t transform_x;
    int32_t transform_y;
};

struct SerializedDisplayListState {
    uint32_t object_count;
    SerializedDisplayObject objects[MAX_DISPLAY_OBJECTS];
};

struct CoreStatePayload {
    CoreStateHeader header;
    RetroFlashMemoryMap memory_map;
    SerializedAVM2State avm2_state;
    SerializedDisplayListState display_list_state;
};

#pragma pack(pop)

#endif // SERIALIZATION_H
