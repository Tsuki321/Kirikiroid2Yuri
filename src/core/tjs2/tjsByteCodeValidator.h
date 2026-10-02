#pragma once
#include "tjsByteCodeOpcodes.h"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

namespace TJS {
namespace ByteCode {
static const size_t MaximumFileSize = 256u * 1024u * 1024u;
struct Invalid {};
inline void Require(bool condition) {
    if (!condition)
        throw Invalid();
}

class Reader {
    const uint8_t *bytes;
    size_t length, position;

  public:
    Reader(const uint8_t *data, size_t size) : bytes(data), length(size), position(0) {}
    size_t Remaining() const { return length - position; }
    const uint8_t *Take(size_t size) {
        Require(size <= Remaining());
        const uint8_t *result = bytes + position;
        position += size;
        return result;
    }
    uint16_t U16() {
        const uint8_t *p = Take(2);
        return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
    }
    uint32_t U32() {
        const uint8_t *p = Take(4);
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    }
    int32_t I32() { return static_cast<int32_t>(U32()); }
    size_t Count(size_t stride) {
        size_t count = U32();
        Require(count <= Remaining() / stride);
        return count;
    }
    void Align4(size_t size) { Take((4 - (size & 3)) & 3); }
    Reader Part(size_t size) { return Reader(Take(size), size); }
    void End() const { Require(Remaining() == 0); }
};

struct Object {
    int32_t parent, setter, getter, superGetter;
    int32_t minRegister, maxRegister;
    size_t constants;
    std::vector<int32_t> code, superPointers;
    std::vector<uint8_t> boundaries;
};

inline bool Reference(int32_t index, size_t count) {
    return index == -1 || (index >= 0 && static_cast<size_t>(index) < count);
}

inline void Instructions(Object &object) {
    const std::vector<int32_t> &code = object.code;
    const size_t total = code.size();
    object.boundaries.assign(total, 0);
    std::vector<size_t> jumps;
    size_t pc = 0;
    auto available = [&](size_t count) { Require(count <= total - pc); };
    auto reg = [&](size_t operand) {
        available(operand + 1);
        Require(code[pc + operand] >= object.minRegister && code[pc + operand] <= object.maxRegister);
    };
    auto data = [&](size_t operand) {
        available(operand + 1);
        Require(code[pc + operand] >= 0 && static_cast<size_t>(code[pc + operand]) < object.constants);
    };
    auto jump = [&](size_t operand) {
        available(operand + 1);
        int64_t target = static_cast<int64_t>(pc) + code[pc + operand];
        Require(target >= 0 && static_cast<uint64_t>(target) < total);
        jumps.push_back(static_cast<size_t>(target));
    };
    int32_t last = VM_RET;
    while (pc < total) {
        object.boundaries[pc] = 1;
        int32_t op = code[pc];
        last = op;
        size_t count = 0;
        if (op >= VM_LOR && op <= VM_MULP) {
            int variant = (op - VM_LOR) % 4;
            reg(1);
            reg(2);
            if (variant == 0)
                count = 3;
            else if (variant == 3) {
                reg(3);
                count = 4;
            } else {
                if (variant == 1)
                    data(3);
                else
                    reg(3);
                reg(4);
                count = 5;
            }
        } else if (op >= VM_INC && op <= VM_DECP) {
            int variant = (op - VM_INC) % 4;
            reg(1);
            if (variant == 0)
                count = 2;
            else {
                reg(2);
                count = variant == 3 ? 3 : 4;
                if (variant == 1)
                    data(3);
                if (variant == 2)
                    reg(3);
            }
        } else {
            switch (op) {
            case VM_NOP:
            case VM_NF:
            case VM_RET:
            case VM_EXTRY:
            case VM_REGMEMBER:
            case VM_DEBUGGER:
                count = 1;
                break;
            case VM_CONST:
                reg(1);
                data(2);
                count = 3;
                break;
            case VM_CP:
            case VM_CEQ:
            case VM_CDEQ:
            case VM_CLT:
            case VM_CGT:
            case VM_CHKINS:
            case VM_SETP:
            case VM_GETP:
            case VM_CHGTHIS:
            case VM_ADDCI:
                reg(1);
                reg(2);
                count = 3;
                break;
            case VM_CL:
            case VM_TT:
            case VM_TF:
            case VM_SETF:
            case VM_SETNF:
            case VM_LNOT:
            case VM_BNOT:
            case VM_TYPEOF:
            case VM_EVAL:
            case VM_EEXP:
            case VM_ASC:
            case VM_CHR:
            case VM_NUM:
            case VM_CHS:
            case VM_INV:
            case VM_CHKINV:
            case VM_INT:
            case VM_REAL:
            case VM_STR:
            case VM_OCTET:
            case VM_SRV:
            case VM_THROW:
            case VM_GLOBAL:
                reg(1);
                count = 2;
                break;
            case VM_CCL:
                reg(1);
                available(3);
                Require(code[pc + 2] >= 0 &&
                        int64_t(code[pc + 1]) + code[pc + 2] <= int64_t(object.maxRegister) + 1);
                count = 3;
                break;
            case VM_JF:
            case VM_JNF:
            case VM_JMP:
                jump(1);
                count = 2;
                break;
            case VM_ENTRY:
                jump(1);
                reg(2);
                count = 3;
                break;
            case VM_GPD:
            case VM_GPDS:
            case VM_DELD:
            case VM_TYPEOFD:
                reg(1);
                reg(2);
                data(3);
                count = 4;
                break;
            case VM_SPD:
            case VM_SPDE:
            case VM_SPDEH:
            case VM_SPDS:
                reg(1);
                data(2);
                reg(3);
                count = 4;
                break;
            case VM_GPI:
            case VM_GPIS:
            case VM_SPI:
            case VM_SPIE:
            case VM_SPIS:
            case VM_DELI:
            case VM_TYPEOFI:
                reg(1);
                reg(2);
                reg(3);
                count = 4;
                break;
            case VM_CALL:
            case VM_CALLD:
            case VM_CALLI:
            case VM_NEW: {
                reg(1);
                reg(2);
                size_t start = (op == VM_CALLD || op == VM_CALLI) ? 5 : 4;
                if (op == VM_CALLD)
                    data(3);
                if (op == VM_CALLI)
                    reg(3);
                available(start);
                int32_t arguments = code[pc + start - 1];
                if (arguments == -1) {
                    count = start;
                    break;
                }
                if (arguments == -2) {
                    available(++start);
                    arguments = code[pc + start - 1];
                    Require(arguments >= 0 && size_t(arguments) <= (total - pc - start) / 2);
                    count = start + size_t(arguments) * 2;
                    for (int32_t i = 0; i < arguments; ++i) {
                        int32_t type = code[pc + start + size_t(i) * 2];
                        Require(type >= 0 && type <= 2);
                        if (type != 2)
                            reg(start + size_t(i) * 2 + 1);
                    }
                } else {
                    Require(arguments >= 0 && size_t(arguments) <= total - pc - start);
                    count = start + size_t(arguments);
                    for (size_t i = start; i < count; ++i)
                        reg(i);
                }
                break;
            }
            default:
                throw Invalid();
            }
        }
        available(count);
        pc += count;
    }
    for (size_t destination : jumps)
        Require(object.boundaries[destination] != 0);
    Require(total == 0 || last == VM_RET || last == VM_JMP || last == VM_THROW);
}

inline bool Validate(const uint8_t *bytes, size_t size) {
    if (!bytes || size < 20 || size > MaximumFileSize)
        return false;
    try {
        Reader file(bytes, size);
        Require(std::memcmp(file.Take(8), "TJS2100", 8) == 0);
        Require(file.U32() == size);
        Require(file.U32() == 0x41544144u); // DATA
        size_t dataSize = file.U32();
        Require(dataSize >= 8);
        Reader data = file.Part(dataSize - 8);
        size_t pools[11] = {};
        const unsigned types[] = {6, 7, 8, 9, 5};
        const size_t strides[] = {1, 2, 4, 8, 8};
        for (size_t i = 0; i < 5; ++i) {
            size_t count = data.Count(strides[i]);
            pools[types[i]] = count;
            data.Take(count * strides[i]);
            data.Align4(count * strides[i]);
        }
        for (unsigned type : {3u, 4u}) {
            size_t count = data.Count(4);
            pools[type] = count;
            for (size_t i = 0; i < count; ++i) {
                size_t length = data.Count(type == 3 ? 2 : 1);
                size_t byteLength = length * (type == 3 ? 2 : 1);
                data.Take(byteLength);
                data.Align4(byteLength);
            }
        }
        data.End();
        Require(file.U32() == 0x534a424fu); // OBJS
        size_t objectsSize = file.U32();
        Require(objectsSize >= 16);
        Reader objects = file.Part(objectsSize - 8);
        file.End();
        int32_t top = objects.I32();
        size_t objectCount = objects.Count(8 + 12 * 4 + 5 * 4);
        Require(objectCount != 0 && Reference(top, objectCount));
        pools[2] = pools[10] = objectCount;
        std::vector<Object> parsed(objectCount);
        for (size_t i = 0; i < objectCount; ++i) {
            Require(objects.U32() == 0x32534a54u); // TJS2
            size_t objectSize = objects.U32();     // Per-object size excludes its header.
            Reader object = objects.Part(objectSize);
            Object &info = parsed[i];
            info.parent = object.I32();
            int32_t name = object.I32();
            Require(Reference(info.parent, objectCount) && info.parent != static_cast<int32_t>(i));
            Require(Reference(name, pools[3]));
            int32_t context = object.I32();
            Require(context >= 0 && context <= 7);
            int32_t variables = object.I32(), reserved = object.I32(), frames = object.I32();
            Require(variables >= 0 && reserved >= 0 && frames >= 0);
            Require(int64_t(variables) + reserved + frames <= 1048576);
            info.minRegister = -variables - reserved;
            info.maxRegister = frames;
            int32_t arguments = object.I32(), unnamed = object.I32(), collapse = object.I32();
            Require(arguments >= 0 && arguments <= variables && unnamed >= -1 && unnamed <= variables &&
                    collapse >= -1 && collapse <= variables);
            info.setter = object.I32();
            info.getter = object.I32();
            info.superGetter = object.I32();
            Require(Reference(info.setter, objectCount) && Reference(info.getter, objectCount) &&
                    Reference(info.superGetter, objectCount));
            size_t debug = object.Count(8);
            object.Take(debug * 8);
            size_t instructions = object.Count(2);
            info.code.reserve(instructions);
            for (size_t j = 0; j < instructions; ++j)
                info.code.push_back(static_cast<int16_t>(object.U16()));
            object.Align4(instructions * 2);
            info.constants = object.Count(4);
            for (size_t j = 0; j < info.constants; ++j) {
                unsigned type = object.U16(), index = object.U16();
                Require(type <= 10);
                if (type > 1)
                    Require(index < pools[type]);
            }
            size_t pointers = object.Count(4);
            for (size_t j = 0; j < pointers; ++j)
                info.superPointers.push_back(object.I32());
            size_t properties = object.Count(8);
            Require(properties == 0 || info.parent >= 0);
            for (size_t j = 0; j < properties; ++j) {
                Require(object.U32() < pools[3]);
                Require(object.U32() < objectCount);
            }
            object.End();
            Instructions(info);
        }
        objects.End();
        // Parent cycles otherwise turn diagnostics and inheritance into recursion.
        std::vector<uint8_t> state(objectCount, 0);
        for (size_t i = 0; i < objectCount; ++i) {
            int32_t node = static_cast<int32_t>(i);
            while (node >= 0 && state[node] == 0) {
                state[node] = 1;
                node = parsed[node].parent;
            }
            Require(node < 0 || state[node] != 1);
            node = static_cast<int32_t>(i);
            while (node >= 0 && state[node] == 1) {
                state[node] = 2;
                node = parsed[node].parent;
            }
            if (!parsed[i].superPointers.empty()) {
                Require(parsed[i].superGetter >= 0);
                const auto &boundaries = parsed[parsed[i].superGetter].boundaries;
                for (int32_t pointer : parsed[i].superPointers)
                    Require(pointer >= 0 && size_t(pointer) < boundaries.size() && boundaries[pointer]);
            }
        }
        return true;
    } catch (const Invalid &) {
        return false;
    }
}
} // namespace ByteCode
} // namespace TJS
