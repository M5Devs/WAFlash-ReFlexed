#include "avm2_vm.h"
#include <cstring>
#include <iostream>

AVM2VM::AVM2VM() : m_retro_memory(nullptr) {
    reset();
}

AVM2VM::~AVM2VM() {}

void AVM2VM::reset() {
    m_stack.clear();
    m_locals.clear();
    m_locals.resize(16, AVM2Value::make_undefined());
}

void AVM2VM::set_retro_memory(RetroFlashMemoryMap* memory_map) {
    m_retro_memory = memory_map;
}

void AVM2VM::push(const AVM2Value& val) {
    m_stack.push_back(val);
}

AVM2Value AVM2VM::pop() {
    if (m_stack.empty()) {
        return AVM2Value::make_undefined();
    }
    AVM2Value val = m_stack.back();
    m_stack.pop_back();
    return val;
}

AVM2Value AVM2VM::top() const {
    if (m_stack.empty()) {
        return AVM2Value::make_undefined();
    }
    return m_stack.back();
}

void AVM2VM::set_local(size_t index, const AVM2Value& val) {
    if (index >= m_locals.size()) {
        m_locals.resize(index + 1, AVM2Value::make_undefined());
    }
    m_locals[index] = val;
}

AVM2Value AVM2VM::get_local(size_t index) const {
    if (index < m_locals.size()) {
        return m_locals[index];
    }
    return AVM2Value::make_undefined();
}

void AVM2VM::sync_to_retro_memory(RetroFlashMemoryMap& memory_map, uint32_t score, uint32_t hp, uint32_t lives, uint32_t stage) {
    memory_map.player_score = score;
    memory_map.player_hp = hp;
    memory_map.player_lives = lives;
    memory_map.stage_id = stage;
}

bool AVM2VM::execute(const uint8_t* bytecode, size_t size, const ABCFile* abc) {
    if (!bytecode || size == 0) return true;

    size_t ip = 0;
    while (ip < size) {
        uint8_t opcode = bytecode[ip++];
        switch (opcode) {
            // Push Constants & Literals
            case 0x24: { // pushbyte: 8-bit signed int
                if (ip >= size) return false;
                int8_t b = static_cast<int8_t>(bytecode[ip++]);
                push(AVM2Value::make_int(static_cast<int32_t>(b)));
                break;
            }
            case 0x25: { // pushshort: u30 variable length int
                uint32_t val = 0;
                if (!ABCParser::read_u30(bytecode, size, ip, val)) return false;
                push(AVM2Value::make_int(static_cast<int32_t>(val)));
                break;
            }
            case 0x2D: { // pushint: u30 index into constant_integers
                uint32_t index = 0;
                if (!ABCParser::read_u30(bytecode, size, ip, index)) return false;
                int32_t val = 0;
                if (abc && index < abc->constant_integers.size()) {
                    val = abc->constant_integers[index];
                }
                push(AVM2Value::make_int(val));
                break;
            }
            case 0x2C: { // pushstring: u30 index into constant_strings
                uint32_t index = 0;
                if (!ABCParser::read_u30(bytecode, size, ip, index)) return false;
                std::string s = "";
                if (abc && index < abc->constant_strings.size()) {
                    s = abc->constant_strings[index];
                }
                push(AVM2Value::make_string(s));
                break;
            }
            case 0x26: // pushtrue
                push(AVM2Value::make_bool(true));
                break;
            case 0x27: // pushfalse
                push(AVM2Value::make_bool(false));
                break;
            case 0x20: // pushnull
                push(AVM2Value::make_null());
                break;
            case 0x21: // pushundefined
                push(AVM2Value::make_undefined());
                break;

            // Local Registers
            case 0x62: { // getlocal: index u30
                uint32_t reg = 0;
                if (!ABCParser::read_u30(bytecode, size, ip, reg)) return false;
                push(get_local(reg));
                break;
            }
            case 0xD0: case 0xD1: case 0xD2: case 0xD3: { // getlocal_0 .. getlocal_3
                push(get_local(opcode - 0xD0));
                break;
            }
            case 0x63: { // setlocal: index u30
                uint32_t reg = 0;
                if (!ABCParser::read_u30(bytecode, size, ip, reg)) return false;
                AVM2Value val = pop();
                set_local(reg, val);
                break;
            }
            case 0xD4: case 0xD5: case 0xD6: case 0xD7: { // setlocal_0 .. setlocal_3
                AVM2Value val = pop();
                set_local(opcode - 0xD4, val);
                break;
            }

            // Arithmetic & Logic
            case 0xA0: { // add
                AVM2Value b = pop();
                AVM2Value a = pop();
                if (a.type == AVM2ValueType::String || b.type == AVM2ValueType::String) {
                    push(AVM2Value::make_string(a.as_string() + b.as_string()));
                } else if (a.type == AVM2ValueType::Number || b.type == AVM2ValueType::Number) {
                    push(AVM2Value::make_number(a.as_number() + b.as_number()));
                } else {
                    push(AVM2Value::make_int(a.as_int() + b.as_int()));
                }
                break;
            }
            case 0xA1: { // subtract
                AVM2Value b = pop();
                AVM2Value a = pop();
                if (a.type == AVM2ValueType::Number || b.type == AVM2ValueType::Number) {
                    push(AVM2Value::make_number(a.as_number() - b.as_number()));
                } else {
                    push(AVM2Value::make_int(a.as_int() - b.as_int()));
                }
                break;
            }
            case 0xA2: { // multiply
                AVM2Value b = pop();
                AVM2Value a = pop();
                if (a.type == AVM2ValueType::Number || b.type == AVM2ValueType::Number) {
                    push(AVM2Value::make_number(a.as_number() * b.as_number()));
                } else {
                    push(AVM2Value::make_int(a.as_int() * b.as_int()));
                }
                break;
            }
            case 0xA3: { // divide
                AVM2Value b = pop();
                AVM2Value a = pop();
                double val_b = b.as_number();
                if (val_b == 0.0) {
                    push(AVM2Value::make_number(0.0));
                } else {
                    push(AVM2Value::make_number(a.as_number() / val_b));
                }
                break;
            }
            case 0xC0: { // increment_i
                AVM2Value val = pop();
                push(AVM2Value::make_int(val.as_int() + 1));
                break;
            }
            case 0xC1: { // decrement_i
                AVM2Value val = pop();
                push(AVM2Value::make_int(val.as_int() - 1));
                break;
            }

            // Control Flow
            case 0x47: // returnvoid
                return true;
            case 0x48: // returnvalue
                return true;

            default:
                // Unknown opcode: ignore or stop
                break;
        }
    }

    if (m_retro_memory) {
        // Direct synchronization hook: if registers 1..4 contain score, hp, lives, stage
        uint32_t score = get_local(1).as_int();
        uint32_t hp = get_local(2).as_int();
        uint32_t lives = get_local(3).as_int();
        uint32_t stage = get_local(4).as_int();

        if (score > 0 || hp > 0 || lives > 0 || stage > 0) {
            sync_to_retro_memory(*m_retro_memory, score, hp, lives, stage);
        }
    }

    return true;
}
