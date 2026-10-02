#include <gtest/gtest.h>
#include "tjsCommHead.h"
#include "tjsError.h"
#include "tjsHashSearch.h"
#include "TickCount.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <iostream>
#include <map>
#include <utility>

// Supply the application's clock, logging and localization hooks. The runtime and its
// compiler, objects, native classes and serializers use the production code.
tjs_uint64 TVPGetTickCount() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
tjs_uint32 TVPGetRoughTickCount32() { return static_cast<tjs_uint32>(TVPGetTickCount()); }

ttstr TVPGetMessageByLocale(const std::string &key) { return ttstr(key.c_str()); }

namespace TJS {
void TVPConsoleLog(const tjs_char *message) {
    std::cerr << ttstr(message).AsStdString() << '\n';
}
} // namespace TJS

namespace {
using Text = std::basic_string<tjs_char>;
std::map<Text, Text> files;

class Reader : public iTJSTextReadStream {
    Text data;
    size_t position = 0;
public:
    explicit Reader(Text value) : data(std::move(value)) {}
    tjs_uint TJS_INTF_METHOD Read(ttstr &target, tjs_uint size) override {
        size_t count = size ? std::min<size_t>(size, data.size() - position) : data.size() - position;
        target = data.substr(position, count).c_str();
        position += count;
        return static_cast<tjs_uint>(count);
    }
    void TJS_INTF_METHOD Destruct() override { delete this; }
};
class Writer : public iTJSTextWriteStream {
    Text name, data;
public:
    explicit Writer(Text path) : name(std::move(path)) {}
    void TJS_INTF_METHOD Write(const ttstr &text) override { data += text.c_str(); }
    void TJS_INTF_METHOD Destruct() override { files[name] = data; delete this; }
};
iTJSTextReadStream *OpenRead(const ttstr &name, const ttstr &) {
    return new Reader(files.at(name.c_str()));
}
iTJSTextWriteStream *OpenWrite(const ttstr &name, const ttstr &) {
    return new Writer(name.c_str());
}

class TJSRuntime : public testing::Test {
protected:
    tTJS *runtime = nullptr;
    decltype(TJSCreateTextStreamForRead) oldRead = nullptr;
    decltype(TJSCreateTextStreamForWrite) oldWrite = nullptr;
    void SetUp() override {
        oldRead = TJSCreateTextStreamForRead;
        oldWrite = TJSCreateTextStreamForWrite;
        TJSCreateTextStreamForRead = OpenRead;
        TJSCreateTextStreamForWrite = OpenWrite;
        runtime = new tTJS();
    }
    void TearDown() override {
        if (runtime) { runtime->Shutdown(); runtime->Release(); }
        files.clear();
        TJSCreateTextStreamForRead = oldRead;
        TJSCreateTextStreamForWrite = oldWrite;
    }
    void Script(const tjs_char *source) {
        try {
            runtime->ExecScript(source, nullptr, nullptr, TJS_W("native-regression"));
        } catch (const eTJSError &error) {
            FAIL() << error.GetMessage().AsStdString();
        }
    }
    tjs_int64 Integer(const tjs_char *expression) {
        tTJSVariant result;
        runtime->EvalExpression(expression, &result);
        return result.AsInteger();
    }
    void InstallCollector() {
        class Collector : public tTJSDispatch {
            tTJS *runtime;
        public:
            explicit Collector(tTJS *engine) : runtime(engine) {}
            tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                tTJSVariant *, tjs_int, tTJSVariant **, iTJSDispatch2 *) override {
                runtime->DoGarbageCollection();
                return TJS_S_OK;
            }
        };
        auto collector = new Collector(runtime);
        tTJSVariant function(collector);
        collector->Release();
        auto global = runtime->GetGlobalNoAddRef();
        ASSERT_EQ(TJS_S_OK, global->PropSet(TJS_MEMBERENSURE, TJS_W("collect"), nullptr,
                                          &function, global));
    }
};

TEST_F(TJSRuntime, ArrayCreationNativeMethodsAndTextStreams) {
    Script(TJS_W("var a = ['ENGINE_CI_STARTED']; a.add('PASS'); a.save('result.txt');"
                 "var b = []; b.load('result.txt');"));
    EXPECT_EQ(2, Integer(TJS_W("b.count")));
    EXPECT_EQ(1, Integer(TJS_W("b[0] == 'ENGINE_CI_STARTED' && b[1] == 'PASS'")));
}

TEST_F(TJSRuntime, EmptyStringsRemainValidWithOptimization) {
    ttstr empty;
    EXPECT_EQ(0, empty.GetLen());
    EXPECT_EQ(0, empty.AsInteger());
    EXPECT_TRUE(empty.Trim().IsEmpty());
    EXPECT_TRUE(ttstr(TJS_W("\t\r\n")).Trim().IsEmpty());
    EXPECT_TRUE(ttstr(TJS_W("\tvalue\r\n")).Trim() == TJS_W("value"));
    tjs_char *buffer = empty.AllocBuffer(0);
    ASSERT_NE(nullptr, buffer);
    EXPECT_EQ(0, buffer[0]);
    EXPECT_EQ(0, empty.GetLen());
    empty.FixLen();
    tTJSVariant value(TJS_W("")), number;
    EXPECT_EQ(0, value.AsInteger());
    EXPECT_EQ(0.0, value.AsReal());
    value.AsNumber(number);
    EXPECT_EQ(0, number.AsInteger());
    EXPECT_EQ(0, (+value).AsInteger());
    value.increment();
    EXPECT_EQ(1, value.AsInteger());
    value = TJS_W("");
    value.decrement();
    EXPECT_EQ(-1, value.AsInteger());
    Script(TJS_W("var empty = ''; var lines = ['', 'value', '']; lines.save('empty.txt');"
                 "var copy = []; copy.load('empty.txt');"));
    EXPECT_EQ(0, Integer(TJS_W("empty.length")));
    EXPECT_EQ(1, Integer(TJS_W("empty[0] == '' && empty['0'] == ''")));
    EXPECT_EQ(0, Integer(TJS_W("int empty")));
    EXPECT_EQ(3, Integer(TJS_W("copy.count")));
    EXPECT_EQ(1, Integer(TJS_W("copy[0] == '' && copy[1] == 'value' && copy[2] == ''")));
}

TEST_F(TJSRuntime, ClosuresKeepObjectsAliveAcrossArrayGrowth) {
    Script(TJS_W("class Counter { var value; function Counter() { value = 0; } function next() { return ++value; } }"
                 "var object = new Counter(); var callback = object.next; var values = [];"
                 "for (var i = 0; i < 1024; ++i) values.add(callback());"));
    EXPECT_EQ(1024, Integer(TJS_W("values.count")));
    EXPECT_EQ(1024, Integer(TJS_W("values[1023]")));
    EXPECT_EQ(1025, Integer(TJS_W("callback()")));
}

TEST_F(TJSRuntime, SubclassConstructionAndExceptionsPreserveTheExecutionStack) {
    Script(TJS_W("class Base { var value; function Base() { value = 7; } function read() { return value; } }"
                 "class Child extends Base { function Child() { super.Base(); value = 42; } }"
                 "var object = new Child(); var caught = false;"
                 "try { object.missingMethod(); } catch (error) { caught = true; }"));
    EXPECT_EQ(42, Integer(TJS_W("object.read()")));
    EXPECT_EQ(1, Integer(TJS_W("caught")));
}

TEST_F(TJSRuntime, GarbageCollectionReleasesUnusedObjectsAndAllowsReentry) {
    InstallCollector();
    Script(TJS_W("var finalized = 0;"
                 "class Tracked { function finalize() { ++finalized; collect(); } }"
                 "var object = new Tracked(); object = void;"));
    runtime->DoGarbageCollection();
    EXPECT_EQ(1, Integer(TJS_W("finalized")));
}

TEST_F(TJSRuntime, GarbageCollectionPreservesNestedActiveFrames) {
    InstallCollector();
    Text source = TJS_W("function descend(depth) {");
    // Each call keeps enough locals live to occupy multiple stack blocks.
    for (int i = 0; i < 64; ++i)
        source += Text(TJS_W("var local")) + ttstr(i).c_str() + TJS_W(" = depth;");
    source += TJS_W("if (depth) return descend(depth - 1) + local63;"
                    "collect(); return local0; } var total = descend(32);");
    Script(source.c_str());
    EXPECT_EQ(528, Integer(TJS_W("total")));
}

TEST(TJSFormatting, MixedArgumentsAndFloatingPointRounding) {
    tjs_char buffer[128];
    const Text expected = TJS_W("code=-7 text=hello value=2.38 hex=001a char=Z");
    EXPECT_EQ(expected.size(), TJS_snprintf(buffer, 128,
        TJS_W("code=%d text=%s value=%.2f hex=%04x char=%lc"),
        -7, TJS_W("hello"), 2.375, 0x1au, int(TJS_W('Z'))));
    EXPECT_EQ(expected, Text(buffer));
}

TEST(TJSFormatting, TruncationPreservesBoundsAndReportsRequiredLength) {
    tjs_char buffer[6] = {TJS_W('!'), 0, 0, 0, 0, TJS_W('!')};
    EXPECT_EQ(6, TJS_snprintf(buffer + 1, 4, TJS_W("%s"), TJS_W("abcdef")));
    EXPECT_EQ(Text(TJS_W("abc")), Text(buffer + 1));
    EXPECT_EQ(TJS_W('!'), buffer[0]);
    EXPECT_EQ(TJS_W('!'), buffer[5]);
    EXPECT_EQ(8, TJS_snprintf(nullptr, 0, TJS_W("%s:%d"), TJS_W("hello"), 42));
    EXPECT_EQ(6, TJS_snprintf(buffer, 0, TJS_W("abcdef")));
    EXPECT_EQ(TJS_W('!'), buffer[0]);
    EXPECT_EQ(6, TJS_snprintf(buffer, 1, TJS_W("abcdef")));
    EXPECT_EQ(0, buffer[0]);
}

TEST(TJSFormatting, LargePaddingUsesCharacterCounts) {
    tjs_char buffer[601];
    EXPECT_EQ(600, TJS_snprintf(buffer, 601, TJS_W("%600s"), TJS_W("x")));
    EXPECT_EQ(Text(599, TJS_W(' ')) + TJS_W("x"), Text(buffer));
    EXPECT_EQ(600, TJS_snprintf(buffer, 601, TJS_W("%-600s"), TJS_W("x")));
    EXPECT_EQ(Text(TJS_W("x")) + Text(599, TJS_W(' ')), Text(buffer));
    EXPECT_EQ(600, TJS_snprintf(buffer, 601, TJS_W("%0600d"), 1));
    EXPECT_EQ(Text(599, TJS_W('0')) + TJS_W("1"), Text(buffer));
    EXPECT_EQ(INT_MAX, TJS_snprintf(buffer, 4, TJS_W("%*s"), INT_MAX, TJS_W("x")));
    EXPECT_EQ(Text(TJS_W("   ")), Text(buffer));
}

TEST(TJSFormatting, PositionalArgumentsPrecisionAndInvalidWidths) {
    tjs_char buffer[64];
    EXPECT_EQ(7, TJS_snprintf(buffer, 64, TJS_W("%2$s:%1$d"), 42, TJS_W("text")));
    EXPECT_EQ(Text(TJS_W("text:42")), Text(buffer));
    const tjs_char unterminated[3] = {TJS_W('a'), TJS_W('b'), TJS_W('c')};
    EXPECT_EQ(3, TJS_snprintf(buffer, 64, TJS_W("%.*s"), 3, unterminated));
    EXPECT_EQ(Text(TJS_W("abc")), Text(buffer));
    EXPECT_LT(TJS_snprintf(buffer, 64, TJS_W("%2147483648d"), 1), 0);
    EXPECT_LT(TJS_snprintf(buffer, 64, TJS_W("%.2147483648d"), 1), 0);
    EXPECT_LT(TJS_snprintf(buffer, 64, TJS_W("%*d"), INT_MIN, 1), 0);
    EXPECT_LT(TJS_snprintf(buffer, 64, TJS_W("%0$d"), 1), 0);
}

TEST(TJSHashStorage, AlignedValuesSurviveCollisionsReplacementAndRemoval) {
    using Value = std::shared_ptr<tjs_uint64>;
    tTJSHashTable<tjs_uint32, Value, tTJSHashFunc<tjs_uint32>, 1> values;
    std::weak_ptr<tjs_uint64> removed;
    for (tjs_uint32 i = 0; i < 8; ++i) {
        auto value = std::make_shared<tjs_uint64>(1000 + i);
        if (i == 3) removed = value;
        values.Add(i, value);
    }
    for (tjs_uint32 i = 0; i < 8; ++i) {
        Value *found = values.Find(i);
        ASSERT_NE(nullptr, found);
        EXPECT_EQ(1000 + i, **found);
    }
    values.Add(4, std::make_shared<tjs_uint64>(9999));
    EXPECT_EQ(9999, **values.Find(4));
    EXPECT_TRUE(values.Delete(3));
    EXPECT_TRUE(removed.expired());
    EXPECT_EQ(nullptr, values.Find(3));
    values.Clear();
    EXPECT_EQ(0, values.GetCount());
}

struct alignas(16) AlignedKey {
    tjs_uint64 id;
    bool operator==(const AlignedKey &other) const { return id == other.id; }
};
struct AlignedKeyHash {
    static tjs_uint32 Make(const AlignedKey &key) { return static_cast<tjs_uint32>(key.id); }
};

TEST(TJSHashStorage, AlignedKeysWorkInInlineAndOverflowEntries) {
    tTJSHashTable<AlignedKey, tjs_uint64, AlignedKeyHash, 1> values;
    for (tjs_uint64 i = 0; i < 8; ++i) values.Add(AlignedKey{i}, i * i);
    for (tjs_uint64 i = 0; i < 8; ++i) {
        auto found = values.Find(AlignedKey{i});
        ASSERT_NE(nullptr, found);
        EXPECT_EQ(i * i, *found);
    }
    EXPECT_TRUE(values.Delete(AlignedKey{0}));
    EXPECT_TRUE(values.Delete(AlignedKey{4}));
    EXPECT_EQ(6, values.GetCount());
}
} // namespace
