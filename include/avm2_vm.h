#ifndef AVM2_VM_H
#define AVM2_VM_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <cstdlib>
#include "abc_parser.h"
#include "retro_flash_memory.h"
#include "serialization.h"

enum class AVM2ValueType {
    Undefined,
    Null,
    Boolean,
    Integer,
    Number,
    String
};

struct AVM2Value {
    AVM2ValueType type;
    union {
        bool bool_val;
        int32_t int_val;
        double num_val;
    };
    std::string str_val;

    AVM2Value() : type(AVM2ValueType::Undefined), int_val(0) {}

    static AVM2Value make_undefined() { AVM2Value v; v.type = AVM2ValueType::Undefined; return v; }
    static AVM2Value make_null() { AVM2Value v; v.type = AVM2ValueType::Null; return v; }
    static AVM2Value make_bool(bool b) { AVM2Value v; v.type = AVM2ValueType::Boolean; v.bool_val = b; return v; }
    static AVM2Value make_int(int32_t i) { AVM2Value v; v.type = AVM2ValueType::Integer; v.int_val = i; return v; }
    static AVM2Value make_number(double d) { AVM2Value v; v.type = AVM2ValueType::Number; v.num_val = d; return v; }
    static AVM2Value make_string(const std::string& s) { AVM2Value v; v.type = AVM2ValueType::String; v.str_val = s; return v; }

    int32_t as_int() const {
        switch (type) {
            case AVM2ValueType::Integer: return int_val;
            case AVM2ValueType::Number: return static_cast<int32_t>(num_val);
            case AVM2ValueType::Boolean: return bool_val ? 1 : 0;
            case AVM2ValueType::String: {
                if (str_val.empty()) return 0;
                char* endptr = nullptr;
                long val = std::strtol(str_val.c_str(), &endptr, 10);
                return static_cast<int32_t>(val);
            }
            default: return 0;
        }
    }

    double as_number() const {
        switch (type) {
            case AVM2ValueType::Number: return num_val;
            case AVM2ValueType::Integer: return static_cast<double>(int_val);
            case AVM2ValueType::Boolean: return bool_val ? 1.0 : 0.0;
            case AVM2ValueType::String: {
                if (str_val.empty()) return 0.0;
                char* endptr = nullptr;
                return std::strtod(str_val.c_str(), &endptr);
            }
            default: return 0.0;
        }
    }

    bool as_bool() const {
        switch (type) {
            case AVM2ValueType::Boolean: return bool_val;
            case AVM2ValueType::Integer: return int_val != 0;
            case AVM2ValueType::Number: return num_val != 0.0;
            case AVM2ValueType::String: return !str_val.empty();
            default: return false;
        }
    }

    std::string as_string() const {
        switch (type) {
            case AVM2ValueType::String: return str_val;
            case AVM2ValueType::Integer: return std::to_string(int_val);
            case AVM2ValueType::Number: return std::to_string(num_val);
            case AVM2ValueType::Boolean: return bool_val ? "true" : "false";
            case AVM2ValueType::Null: return "null";
            case AVM2ValueType::Undefined: return "undefined";
            default: return "";
        }
    }
};

class AVM2VM {
private:
    std::vector<AVM2Value> m_stack;
    std::vector<AVM2Value> m_locals;
    RetroFlashMemoryMap* m_retro_memory;

public:
    AVM2VM();
    ~AVM2VM();

    void reset();
    void set_retro_memory(RetroFlashMemoryMap* memory_map);

    const std::vector<AVM2Value>& get_stack() const { return m_stack; }
    const std::vector<AVM2Value>& get_locals() const { return m_locals; }

    void push(const AVM2Value& val);
    AVM2Value pop();
    AVM2Value top() const;

    void set_local(size_t index, const AVM2Value& val);
    AVM2Value get_local(size_t index) const;

    void export_state(SerializedAVM2State& out_state) const;
    void import_state(const SerializedAVM2State& in_state);

    void sync_to_retro_memory(RetroFlashMemoryMap& memory_map, uint32_t score, uint32_t hp, uint32_t lives, uint32_t stage);

    bool execute(const uint8_t* bytecode, size_t size, const ABCFile* abc = nullptr);
};

#endif // AVM2_VM_H
