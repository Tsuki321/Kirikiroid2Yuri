#include <gtest/gtest.h>
#include "tjsCommHead.h"
#include "tjsError.h"
#include "tjsHashSearch.h"
#include "tjsObject.h"
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

static const tjs_char TextRenderTestSource[] =
#include "../src/plugins/TextRenderScript.inc"
;

TEST_F(TJSRuntime, TextRendererWrapsAndKeepsInstancesIndependent) {
    Script(TextRenderTestSource);
    Script(TJS_W("var r = new TextRenderBase(); r.defaultFontSize = 20; r.defaultLineSpacing = 4;"
                 "r.clear(); r.setRenderSize(30, 100); r.render('abcd'); var chars = r.getCharacters();"
                 "var other = new TextRenderBase(); other.render('Z');"));
    EXPECT_EQ(1, Integer(TJS_W("r.renderCount == 4 && r.renderLines == 2 && chars[3].y == 24")));
    EXPECT_EQ(1, Integer(TJS_W("r.renderRight == 30 && r.renderBottom == 44 && !r.renderOver")));
    EXPECT_EQ(1, Integer(TJS_W("r.renderText == 'abcd' && other.renderCount == 1")));
    Script(TJS_W("r.done(); r.done(); r.render('abcde', 0, 1);"));
    EXPECT_EQ(5, Integer(TJS_W("r.renderCount")));
}

TEST_F(TJSRuntime, TextRendererFormattingAndFontCallbacks) {
    Script(TextRenderTestSource);
    Script(TJS_W("var r = new TextRenderBase(); r.defaultFontSize = 20; r.clear();"
                 "r.setRenderSize(500, 100); var calls = 0;"
                 "r.onFontChange = function(info) { ++calls; };"
                 "r.onGetTextWidth = function(text, size) { return size / 2 * text.length; };"
                 "r.render('%b1A%b0%200;B#112233;C#;%rD'); var c = r.getCharacters();"));
    EXPECT_EQ(1, Integer(TJS_W("calls >= 4 && c[0].bold && !c[1].bold")));
    EXPECT_EQ(1, Integer(TJS_W("c[0].size == 20 && c[1].size == 40 && c[3].size == 20")));
    EXPECT_EQ(1, Integer(TJS_W("c[2].color == 0x112233 && c[3].color == 0xffffff")));
}

TEST_F(TJSRuntime, TextRendererRubyExpansionAndInlineGraphics) {
    Script(TextRenderTestSource);
    Script(TJS_W("var r = new TextRenderBase(); r.defaultFontSize = 20; r.clear(); r.setRenderSize(500, 100);"
                 "r.onEval = function(expr) { return 'Hi'; };"
                 "r.onGetGraphSize = function(name) { return %['width'=>18, 'height'=>12]; };"
                 "r.render('[ru,2]AB $name; &icon;'); var c = r.getCharacters();"));
    EXPECT_EQ(7, Integer(TJS_W("r.renderCount")));
    EXPECT_EQ(1, Integer(TJS_W("c[0].ruby[0].text == 'ru' && c[0].ruby[0].x == 5 && c[0].ruby[0].y == -12")));
    EXPECT_EQ(1, Integer(TJS_W("c[6].graph && c[6].text == 'icon' && c[6].width == 18 && r.renderTop == -12")));
}

TEST_F(TJSRuntime, TextRendererLinksAndAlignment) {
    Script(TextRenderTestSource);
    Script(TJS_W("var r = new TextRenderBase(); r.defaultFontSize = 20; r.clear(); r.setRenderSize(100,100);"
                 "r.render('%C%lmore;AB%l;C'); var c = r.getCharacters(); var boxes = r.getLinkRects('more');"));
    EXPECT_EQ(1, Integer(TJS_W("c[0].x == 35 && boxes.count == 1 && boxes[0].width == 20")));
    EXPECT_EQ(1, Integer(TJS_W("r.getLinkNames().join(',') == 'more' && r.getLinkCharacters('more').count == 2")));
    EXPECT_EQ(1, Integer(TJS_W("r.isLinkContains('more', 36, 1) && r.getLinkOfPosition(36,1) == 'more'")));
    EXPECT_EQ(1, Integer(TJS_W("r.getLinkOfPosition(90,1) === void && !r.contains(0,0)")));
}

TEST_F(TJSRuntime, TextRendererTimingAndKeyWaits) {
    Script(TextRenderTestSource);
    Script(TJS_W("var r = new TextRenderBase(); r.timeScale = 10; r.setRenderSize(500,100);"
                 "r.render('A%w200;B\\\\k%t30;C');"));
    EXPECT_EQ(80, Integer(TJS_W("r.renderDelay")));
    EXPECT_EQ(1, Integer(TJS_W("r.calcShowCount(9) == 0 && r.calcShowCount(10) == 1 && r.calcShowCount(40) == 2 && r.calcShowCount(80) == 3")));
    Script(TJS_W("var waits = r.getKeyWait(), chars = r.getCharacters();"));
    EXPECT_EQ(1, Integer(TJS_W("waits.count == 1 && waits[0].pos == 2 && waits[0].time == 40")));
    EXPECT_EQ(0, Integer(TJS_W("r.getKeyWait(3).count")));
    EXPECT_EQ(1, Integer(TJS_W("chars[0].delay == 10 && chars[1].delay == 40 && chars[2].delay == 80")));
    Script(TJS_W("waits.shift();"));
    EXPECT_EQ(1, Integer(TJS_W("r.getKeyWait().count")));
}

TEST_F(TJSRuntime, TextRendererStyleColorsAndSkipTiming) {
    Script(TextRenderTestSource);
    Script(TJS_W("var r = new TextRenderBase();"
                 "r.setFont(%['fontSize'=>20,'chColor'=>0x123456,'edge'=>true,'edgeColor'=>0,'shadow'=>false]);"
                 "r.render('A'); var first = r.getCharacters()[0];"));
    EXPECT_EQ(1, Integer(TJS_W("first.size == 20 && first.color == 0x123456 && first.edge === 0 && first.shadow === void")));
    Script(TJS_W("r.clear(); r.timeScale = 10; r.setOption(%['ignore_delay'=>true]);"
                 "r.render('%d300;A%t500;B\\\\k%a200;C'); var wait = r.getKeyWait()[0];"));
    EXPECT_EQ(1, Integer(TJS_W("r.renderDelay == 30 && wait.pos == 2 && wait.time == 20")));
    EXPECT_EQ(3, Integer(TJS_W("r.calcShowCount(30)")));
}

TEST_F(TJSRuntime, TextRendererUnicodeVerticalAndOverflow) {
    Script(TextRenderTestSource);
    Script(TJS_W("var r = new TextRenderBase(); r.defaultFontSize = 20; r.clear(); r.setRenderSize(20,20);"
                 "r.render('e\u0301\U0001f600'); var c = r.getCharacters();"));
    EXPECT_EQ(2, Integer(TJS_W("r.renderCount")));
    EXPECT_EQ(1, Integer(TJS_W("c[0].text.length == 2 && c[1].text.length == 2 && r.renderOver")));
    Script(TJS_W("r.clear(); r.setOption(%['vertical'=>true]); r.setRenderSize(60,40); r.render('ABC'); c = r.getCharacters();"));
    EXPECT_EQ(1, Integer(TJS_W("c[0].vertical && c[0].x == 40 && c[1].y == 20 && c[2].x < c[0].x")));
}

TEST_F(TJSRuntime, TextRendererRejectsMalformedAndRecursiveControls) {
    Script(TextRenderTestSource);
    Script(TJS_W("var r = new TextRenderBase(); var rejected = 0;"
                 "try { r.render('%funterminated'); } catch (e) { ++rejected; }"
                 "r.clear(); try { r.render('#invalid;'); } catch (e) { ++rejected; }"
                 "r.clear(); r.onEval = function(expr) { return '$loop;'; };"
                 "try { r.render('$loop;'); } catch (e) { ++rejected; }"
                 "r.clear(); r.render('OK');"));
    EXPECT_EQ(3, Integer(TJS_W("rejected")));
    EXPECT_EQ(2, Integer(TJS_W("r.renderCount")));
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

TEST(TJSAllocator, NullAndEmptyAllocationsCanBeReleased) {
    TJS_free(nullptr);
    TJS_free(TJS_malloc(0));
}

TEST(TJSAllocator, GrowthPreservesBytesAndOverflowPreservesTheOriginalAllocation) {
    auto data = static_cast<unsigned char *>(TJS_realloc(nullptr, 16));
    ASSERT_NE(nullptr, data);
    for (unsigned char i = 0; i < 16; ++i) data[i] = i;
    auto grown = static_cast<unsigned char *>(TJS_realloc(data, 64));
    if (!grown) TJS_free(data);
    ASSERT_NE(nullptr, grown);
    for (unsigned char i = 0; i < 16; ++i) EXPECT_EQ(i, grown[i]);
    EXPECT_EQ(nullptr, TJS_malloc(static_cast<size_t>(-1)));
    EXPECT_EQ(nullptr, TJS_realloc(grown, static_cast<size_t>(-1)));
    for (unsigned char i = 0; i < 16; ++i) EXPECT_EQ(i, grown[i]);
    TJS_free(grown);
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
