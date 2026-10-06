#include <gtest/gtest.h>
#include "tjsByteCodeValidator.h"
#include <random>
#include <utility>
using namespace TJS;
namespace {
void Put32(std::vector<uint8_t> &out, uint32_t value) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}
void Put16(std::vector<uint8_t> &out, uint16_t value) {
    out.push_back(value & 255);
    out.push_back(value >> 8);
}
void Chunk(std::vector<uint8_t> &out, uint32_t tag, const std::vector<uint8_t> &payload,
           bool includesHeader) {
    Put32(out, tag);
    Put32(out, payload.size() + (includesHeader ? 8 : 0));
    out.insert(out.end(), payload.begin(), payload.end());
}
std::vector<uint8_t> Program(const std::vector<int16_t> &code,
                             const std::vector<std::pair<uint16_t, uint16_t>> &constants = {},
                             const std::vector<uint32_t> &entries = {},
                             int32_t context = 0, int32_t frames = 4, int32_t superGetter = -1) {
    std::vector<uint8_t> data(7 * 4, 0), object, objects, file;
    const int32_t metadata[] = {-1, -1, context, 0, 2, frames, 0, 0, -1, -1, -1, superGetter};
    for (int32_t field : metadata)
        Put32(object, field);
    Put32(object, 0); // source positions
    Put32(object, code.size());
    for (int16_t instruction : code)
        Put16(object, instruction);
    while (object.size() % 4)
        object.push_back(0);
    Put32(object, constants.size());
    for (const auto &constant : constants) {
        Put16(object, constant.first);
        Put16(object, constant.second);
    }
    Put32(object, entries.size());
    for (auto entry : entries)
        Put32(object, entry);
    Put32(object, 0); // properties
    Put32(objects, 0);
    Put32(objects, 1);
    Chunk(objects, 0x32534a54, object, false);
    const char tag[] = "TJS2100";
    file.insert(file.end(), tag, tag + 8);
    Put32(file, 12 + 8 + data.size() + 8 + objects.size());
    Chunk(file, 0x41544144, data, true);
    Chunk(file, 0x534a424f, objects, true);
    return file;
}
bool Valid(const std::vector<uint8_t> &bytes) {
    return ByteCode::Validate(bytes.data(), bytes.size());
}
} // namespace

TEST(Bytecode, AcceptsEmptyScriptAndUnnamedContext) {
    EXPECT_TRUE(Valid(Program({VM_RET})));
}
TEST(Bytecode, RejectsEveryTruncatedPrefix) {
    auto bytes = Program({VM_CONST, 1, 0, VM_SRV, 1, VM_RET}, {{0, 0}});
    ASSERT_TRUE(Valid(bytes));
    for (size_t size = 0; size < bytes.size(); ++size)
        EXPECT_FALSE(ByteCode::Validate(bytes.data(), size)) << size;
    EXPECT_FALSE(ByteCode::Validate(nullptr, 0));
    EXPECT_FALSE(ByteCode::Validate(nullptr, 100));
}
TEST(Bytecode, RejectsMissingOperandsAndOutOfRangeRegisters) {
    EXPECT_FALSE(Valid(Program({VM_CONST})));
    EXPECT_FALSE(Valid(Program({VM_CP, 1, 5, VM_RET})));
    EXPECT_FALSE(Valid(Program({VM_CP, 1, -3, VM_RET})));
    EXPECT_FALSE(Valid(Program({VM_CCL, 1, -1, VM_RET})));
    EXPECT_FALSE(Valid(Program({VM_CCL, 1, 8, VM_RET})));
    EXPECT_FALSE(Valid(Program({VM_NOP})));
    EXPECT_FALSE(Valid(Program({32767, VM_RET})));
}
TEST(Bytecode, RejectsInvalidConstantsAndObjectReferences) {
    EXPECT_FALSE(Valid(Program({VM_CONST, 1, 0, VM_RET})));
    EXPECT_FALSE(Valid(Program({VM_RET}, {{3, 0}}))); // absent string pool
    EXPECT_FALSE(Valid(Program({VM_RET}, {{2, 1}}))); // absent object
    EXPECT_FALSE(Valid(Program({VM_RET}, {{65535, 0}})));
    EXPECT_TRUE(Valid(Program({VM_CONST, 1, 0, VM_RET}, {{0, 0}})));
}
TEST(Bytecode, BranchesMustLandOnInstructions) {
    EXPECT_TRUE(Valid(Program({VM_JMP, 2, VM_RET})));
    EXPECT_FALSE(Valid(Program({VM_JMP, 1, VM_RET})));
    EXPECT_FALSE(Valid(Program({VM_JMP, -1, VM_RET})));
    EXPECT_FALSE(Valid(Program({VM_JMP, 3, VM_RET})));
}
TEST(Bytecode, ChecksVariableArgumentInstructionLengths) {
    EXPECT_TRUE(Valid(Program({VM_CALL, 1, 2, 0, VM_RET})));
    EXPECT_TRUE(Valid(Program({VM_CALL, 1, 2, -1, VM_RET})));
    EXPECT_TRUE(Valid(Program({VM_CALL, 1, 2, -2, 1, 0, 3, VM_RET})));
    EXPECT_FALSE(Valid(Program({VM_CALL, 1, 2, 100, VM_RET})));
    EXPECT_FALSE(Valid(Program({VM_CALL, 1, 2, -2, 1, 99, 3, VM_RET})));
    EXPECT_FALSE(Valid(Program({VM_CALL, 1, 2, -2, 1, 0})));
}
TEST(Bytecode, MutatedHeadersAndCountsRemainBounded) {
    auto original = Program({VM_CONST, 1, 0, VM_SRV, 1, VM_RET}, {{0, 0}});
    std::mt19937 random(0x4b524b52);
    for (int i = 0; i < 20000; ++i) {
        auto mutated = original;
        for (unsigned j = 0, count = 1 + random() % 8; j < count; ++j)
            mutated[random() % mutated.size()] = static_cast<uint8_t>(random());
        (void)Valid(mutated); // ASan/UBSan check every parser path reached here.
    }
}

TEST(Bytecode, SuperclassGetterEntriesAllowUnreachablePadding) {
    EXPECT_TRUE(Valid(Program({VM_RET, VM_NOP}, {}, {0})));
    EXPECT_TRUE(Valid(Program({VM_RET, VM_NOP, VM_RET, VM_NOP}, {}, {0, 2})));
    EXPECT_FALSE(Valid(Program({VM_RET, VM_NOP}, {}, {1})));
    EXPECT_FALSE(Valid(Program({VM_RET, VM_NOP}, {}, {2})));
    EXPECT_FALSE(Valid(Program({VM_JMP, 3, VM_RET, VM_NOP})));
}

TEST(Bytecode, LegacyInheritanceTemporaryNeedsARealAllocatedSlot) {
    EXPECT_TRUE(Valid(Program({VM_CP, 2, 1, VM_RET}, {}, {}, 6, 1, 0)));
    EXPECT_FALSE(Valid(Program({VM_CP, 3, 1, VM_RET}, {}, {}, 6, 1, 0)));
    EXPECT_FALSE(Valid(Program({VM_CP, 2, 1, VM_RET}, {}, {}, 0, 1, 0)));
    EXPECT_FALSE(Valid(Program({VM_CP, 2, 1, VM_RET}, {}, {}, 6, 1, -1)));
    EXPECT_FALSE(Valid(Program({VM_CP, 1, 0, VM_RET}, {}, {}, 6, 0, 0)));
}
