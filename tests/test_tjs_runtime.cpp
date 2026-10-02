#include <gtest/gtest.h>
#include "tjsCommHead.h"
#include "tjsError.h"
#include "TickCount.h"
#include <chrono>
#include <map>

// These are the interpreter's only host clock hooks. The runtime and its
// compiler, objects, native classes and serializers use the production code.
tjs_uint64 TVPGetTickCount() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
tjs_uint32 TVPGetRoughTickCount32() { return static_cast<tjs_uint32>(TVPGetTickCount()); }

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
    EXPECT_EQ(nullptr, empty.AllocBuffer(0));
    empty.FixLen();
    tTJSVariant value(TJS_W("")), number;
    EXPECT_EQ(0, value.AsInteger());
    EXPECT_EQ(0.0, value.AsReal());
    value.ToNumber(number);
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
} // namespace
