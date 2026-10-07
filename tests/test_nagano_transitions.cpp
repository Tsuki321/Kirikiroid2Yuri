#include <gtest/gtest.h>
#include "NaganoTransitions.h"
#include <set>

using namespace NaganoTransitions;

namespace {
constexpr int Width = 64, Height = 48;
const Effect Effects[] = { Effect::Universal3D, Effect::BlurFade, Effect::ScanLine, Effect::ZoomFade,
    Effect::RGBFade, Effect::Spin, Effect::Flutter, Effect::Book, Effect::ImageWipe,
    Effect::HoneyTurn, Effect::Morphing, Effect::MultiRipple };

Options options(Effect effect, int w = Width, int h = Height) {
    Options o;
    o.effect = effect; o.time = 1000;
    o.speed1 = 0.25; o.speed2 = 0.125;
    o.blur1x = 6; o.blur2y = 6;
    o.zoom1 = 160; o.zoom2 = 200;
    o.delay = {{128, 64, 0, 0}};
    o.dir = effect == Effect::HoneyTurn ? 6 : 0;
    o.size = 12; o.twist = 2;
    o.count = 3; o.rwidth = 12; o.maxdrift = 8;
    o.before = {0, 0, double(w), 0, 0, double(h), double(w), 0, double(w), double(h), 0, double(h)};
    o.after = o.before;
    for (size_t i = 0; i < o.after.size(); i += 2) o.after[i] += 8;
    return o;
}

std::vector<uint32_t> picture(bool second, int w = Width, int h = Height) {
    std::vector<uint32_t> p(size_t(w) * h);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
        p[size_t(y) * w + x] = 0xff000000u | ((x * x + 13 * y) & 255) |
            (((y * 5 + (second ? 137 : 0)) & 255) << 8) | (((x * 3 + y + (second ? 51 : 0)) & 255) << 16);
    return p;
}

Transition transition(Effect effect, int w = Width, int h = Height) {
    std::vector<uint32_t> rule(size_t(w) * h);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
        rule[size_t(y) * w + x] = 0xff000000u | ((x + y) & 31) | (uint32_t(128 + x % 128) << 8);
    return Transition(options(effect, w, h), w, h, std::move(rule), w, h);
}

std::vector<uint32_t> render(Transition &t, uint64_t time, const std::vector<uint32_t> &a,
                              const std::vector<uint32_t> &b, int w = Width, int h = Height) {
    std::vector<uint32_t> out(size_t(w) * h);
    t.setTime(time);
    t.render(Image(a.data(), w * 4), Image(b.data(), w * 4), out.data(), 0, 0, w, h);
    return out;
}
}

TEST(NaganoTransitions, EveryRegisteredEffectHasExactEndpointsIncludingTinyImages) {
    const std::pair<int, int> sizes[] = {{1, 1}, {2, 3}, {Width, Height}};
    for (const auto &size : sizes) {
        const auto a = picture(false, size.first, size.second), b = picture(true, size.first, size.second);
        for (auto effect : Effects) {
            auto t = transition(effect, size.first, size.second);
            EXPECT_EQ(render(t, 0, a, b, size.first, size.second), a);
            EXPECT_EQ(render(t, 1000, a, b, size.first, size.second), b);
            EXPECT_EQ(render(t, std::numeric_limits<uint64_t>::max(), a, b, size.first, size.second), b);
        }
    }
}

TEST(NaganoTransitions, MidpointsUseTwelveDistinctRenderers) {
    const auto a = picture(false), b = picture(true);
    std::set<std::vector<uint32_t>> frames;
    for (auto effect : Effects) {
        auto t = transition(effect);
        auto frame = render(t, 500, a, b);
        EXPECT_NE(frame, a); EXPECT_NE(frame, b);
        frames.insert(std::move(frame));
    }
    EXPECT_EQ(frames.size(), sizeof(Effects) / sizeof(Effects[0]));
}

TEST(NaganoTransitions, DividedNonzeroRegionsAreIdenticalToFullFrames) {
    const auto a = picture(false), b = picture(true);
    for (auto effect : Effects) {
        auto t = transition(effect);
        for (uint64_t time : {1u, 251u, 500u, 999u}) {
            const auto full = render(t, time, a, b);
            std::vector<uint32_t> pieces(full.size(), 0xdeadbeefu);
            t.setTime(time);
            for (int y = Height - 1; y >= 0; --y) {
                for (int x = 0; x < Width; x += 13) {
                    const int count = std::min(13, Width - x);
                    t.render(Image(a.data(), Width * 4), Image(b.data(), Width * 4),
                             pieces.data() + y * Width + x, x, y, count, 1);
                }
            }
            EXPECT_EQ(full, pieces);
        }
    }
}

TEST(NaganoTransitions, HandlesNegativeStrideAndInvalidatesTheFrameAtAnEqualTick) {
    const auto a = picture(false), b = picture(true);
    std::vector<uint32_t> flippedA(a.size()), flippedB(b.size()), out(a.size());
    for (int y = 0; y < Height; ++y) {
        std::copy_n(a.data() + y * Width, Width, flippedA.data() + (Height - 1 - y) * Width);
        std::copy_n(b.data() + y * Width, Width, flippedB.data() + (Height - 1 - y) * Width);
    }
    for (auto effect : Effects) {
        auto t = transition(effect);
        const auto full = render(t, 400, a, b);
        t.setTime(400);
        t.render(Image(flippedA.data() + (Height - 1) * Width, -Width * 4),
                 Image(flippedB.data() + (Height - 1) * Width, -Width * 4), out.data(), 0, 0, Width, Height);
        EXPECT_EQ(out, full);
        const auto changed = render(t, 400, b, a);
        EXPECT_NE(changed, full);
    }
}

TEST(NaganoTransitions, ScanlinePushesOppositeRowsInOppositeDirections) {
    auto o = options(Effect::ScanLine);
    Transition t(o, Width, Height);
    const auto a = picture(false), b = picture(true);
    const auto out = render(t, 500, a, b);
    EXPECT_EQ(out[0], b[Width / 2]);
    EXPECT_EQ(out[Width / 2], a[0]);
    EXPECT_EQ(out[Width], a[Width + Width / 2]);
    EXPECT_EQ(out[Width + Width / 2], b[Width]);
}

TEST(NaganoTransitions, ChannelDelaysApplyToRgbaTextureOrder) {
    auto o = options(Effect::RGBFade);
    o.delay = {{128, 0, 0, 0}};
    Transition t(o, 1, 1);
    const std::vector<uint32_t> a(1, 0xff000000), b(1, 0xffffffff);
    const auto out = render(t, 250, a, b, 1, 1);
    EXPECT_EQ(out[0] & 255u, 0u); // Red is delayed, although it is the low byte on the renderer.
    EXPECT_GT((out[0] >> 8) & 255u, 100u);
    EXPECT_EQ((out[0] >> 8) & 255u, (out[0] >> 16) & 255u);
    EXPECT_EQ(out[0] >> 24, 255u);
}

TEST(NaganoTransitions, BoxBlurSpreadsAnImpulseOnlyAlongTheRequestedAxis) {
    auto o = options(Effect::BlurFade);
    o.blur1x = o.blur2x = 4; o.blur1y = o.blur2y = 0;
    std::vector<uint32_t> a(Width * Height, 0xff000000);
    a[Height / 2 * Width + Width / 2] = 0xff0000ff;
    Transition box(o, Width, Height);
    const auto out = render(box, 500, a, a);
    for (int x = Width / 2 - 2; x <= Width / 2 + 2; ++x)
        EXPECT_EQ(out[Height / 2 * Width + x], 0xff000033u);
    EXPECT_EQ(out[(Height / 2 - 1) * Width + Width / 2], 0xff000000u);
    o.blurtype = 1;
    Transition bilinear(o, Width, Height);
    EXPECT_NE(render(bilinear, 500, a, a), out);
}

TEST(NaganoTransitions, BlurringStraightAlphaDoesNotLeakInvisibleColor) {
    auto o = options(Effect::BlurFade);
    o.alpha = Alpha::Straight; o.blur1x = o.blur2x = 4; o.blur1y = o.blur2y = 0;
    std::vector<uint32_t> a(Width * Height, 0x00ff0000); // Invisible blue.
    a[Height / 2 * Width + Width / 2] = 0xff0000ff; // Opaque red.
    Transition t(o, Width, Height);
    const auto out = render(t, 500, a, a);
    EXPECT_EQ(out[Height / 2 * Width + Width / 2] & 0xffffffu, 0x0000ffu);
    EXPECT_GT(out[Height / 2 * Width + Width / 2] >> 24, 0u);
    EXPECT_LT(out[Height / 2 * Width + Width / 2] >> 24, 255u);
}

TEST(NaganoTransitions, UniversalMovesPixelsUsingTheRuleRatherThanJustFading) {
    auto o = options(Effect::Universal3D);
    o.alpha = Alpha::Straight; o.speed1 = 0.0625; o.speed2 = 0;
    const std::vector<uint32_t> rule(1, 0xff00ff00); // Start zero, speed 255, direction right.
    std::vector<uint32_t> a(Width * Height, 0), b(a.size(), 0);
    a[2 * Width + 5] = 0xff0000ff;
    Transition t(o, Width, Height, rule, 1, 1);
    const auto out = render(t, 500, a, b);
    EXPECT_EQ(out[2 * Width + 5], 0u);
    EXPECT_EQ(out[2 * Width + 8] & 0xffffffu, 0xffu);
    EXPECT_GT(out[2 * Width + 8] >> 24, 0u);
    EXPECT_LT(out[2 * Width + 8] >> 24, 255u);
}

TEST(NaganoTransitions, UniversalHsbRulesMatchRecoveredColorChannelMapping) {
    // RGBA examples cover inverse brightness, doubled saturation and integer
    // hue rounding. The paired RGB rules encode start/speed/direction.
    const uint32_t rules[][2] = {{0xff0000ff, 0xff00ff00},
        {0xff000080, 0xff00ff7f}, {0xff8080ff, 0xff00fe00},
        {0xff00ff00, 0xff55ff00}, {0xffffff00, 0xff7fff00},
        {0xffff00ff, 0xffd4ff00}, {0xff808080, 0xff00007f}};
    const auto a = picture(false), b = picture(true);
    auto o = options(Effect::Universal3D);
    o.speed1 = 0.0625; o.speed2 = 0;
    for (const auto &pair : rules) {
        SCOPED_TRACE(pair[0]);
        o.hsb = true;
        Transition hsb(o, Width, Height, {pair[0]}, 1, 1);
        o.hsb = false;
        Transition rgb(o, Width, Height, {pair[1]}, 1, 1);
        for (uint64_t time : {250u, 500u, 750u})
            EXPECT_EQ(render(hsb, time, a, b), render(rgb, time, a, b));
    }
}

TEST(NaganoTransitions, ImageWipeUsesRuleColorAndAlphaWithBothSweepDirections) {
    auto o = options(Effect::ImageWipe); o.alpha = Alpha::Straight;
    const std::vector<uint32_t> a(16, 0xff000000), b(16, 0xffff0000);
    const std::vector<uint32_t> rule = {0xff0000ff, 0x8000ff00, 0x0000ffff, 0xffffffff};
    Transition t(o, 8, 2, rule, 4, 1);
    const auto out = render(t, 500, a, b, 8, 2);
    EXPECT_EQ(out[0], b[0]);
    EXPECT_EQ(out[2], rule[0]);
    EXPECT_EQ(out[3], over(b[3], rule[1], 255, Alpha::Straight));
    EXPECT_EQ(out[4], b[4]);
    EXPECT_EQ(out[5], a[5]);
    o.dir = 1;
    Transition reverse(o, 8, 2, rule, 4, 1);
    const auto reversed = render(reverse, 500, a, b, 8, 2);
    EXPECT_EQ(reversed[0], a[0]);
    EXPECT_EQ(reversed[7], b[7]);
    EXPECT_NE(reversed, out);
}

TEST(NaganoTransitions, MorphInterpolatesTriangleCoordinatesAndIdentityIsStable) {
    const auto a = picture(false), b = picture(true);
    auto o = options(Effect::Morphing);
    Transition moving(o, Width, Height);
    const auto moved = render(moving, 500, a, b);
    const int x = 20, y = 20;
    EXPECT_EQ(moved[y * Width + x], blend(a[y * Width + x - 4], b[y * Width + x + 4], 127, Alpha::Opaque));
    EXPECT_NE(moved[y * Width + x], blend(a[y * Width + x], b[y * Width + x], 127, Alpha::Opaque));
    o.after = o.before;
    Transition identity(o, Width, Height);
    EXPECT_EQ(render(identity, 500, a, a), a);
}

TEST(NaganoTransitions, RippleSeedIsStableAndAmplitudeControlsDistortion) {
    const auto a = picture(false), b = picture(true);
    auto o = options(Effect::MultiRipple);
    Transition first(o, Width, Height), repeat(o, Width, Height);
    const auto waves = render(first, 600, a, b);
    EXPECT_EQ(render(repeat, 600, a, b), waves);
    o.maxdrift = 0;
    Transition still(o, Width, Height);
    EXPECT_NE(render(still, 600, a, b), waves);
    o.maxdrift = 8; o.seed++;
    Transition different(o, Width, Height);
    EXPECT_NE(render(different, 600, a, b), waves);
}

TEST(NaganoTransitions, TransparentCompositingPreservesAllThreeLayerModes) {
    EXPECT_EQ(over(0, 0xff0000ff, 128, Alpha::Straight), 0x800000ffu);
    EXPECT_EQ(over(0xff000000, 0xff0000ff, 255, Alpha::Opaque), 0xff0000ffu);
    EXPECT_EQ(over(0, 0x80000080, 128, Alpha::Additive), 0x40000040u);
    EXPECT_EQ(over(0x12345678, 0x00ffffff, 255, Alpha::Straight), 0x12345678u);
}

TEST(NaganoTransitions, RejectsInvalidOptionsRulesMeshesAndImageRegions) {
    auto o = options(Effect::ScanLine);
    EXPECT_THROW(Transition(o, 0, Height), std::invalid_argument);
    EXPECT_THROW(Transition(o, 16384, 16384), std::invalid_argument);
    o.time = 0; EXPECT_THROW(Transition(o, Width, Height), std::invalid_argument);
    o = options(Effect::Universal3D);
    EXPECT_THROW(Transition(o, Width, Height), std::invalid_argument);
    o.speed1 = std::numeric_limits<double>::infinity();
    EXPECT_THROW(Transition(o, Width, Height, {0}, 1, 1), std::invalid_argument);
    o = options(Effect::BlurFade); o.exponent = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(Transition(o, Width, Height), std::invalid_argument);
    o = options(Effect::ZoomFade); o.zoom2 = 0;
    EXPECT_THROW(Transition(o, Width, Height), std::invalid_argument);
    o = options(Effect::Morphing); o.after.pop_back();
    EXPECT_THROW(Transition(o, Width, Height), std::invalid_argument);
    o = options(Effect::Morphing); o.before[0] = std::numeric_limits<double>::infinity();
    EXPECT_THROW(Transition(o, Width, Height), std::invalid_argument);
    o = options(Effect::MultiRipple); o.count = 21;
    EXPECT_THROW(Transition(o, Width, Height), std::invalid_argument);
    auto t = transition(Effect::ScanLine);
    const auto a = picture(false), b = picture(true);
    uint32_t pixel;
    EXPECT_THROW(t.render(Image(a.data(), Width * 4), Image(b.data(), Width * 4), &pixel,
                          Width, 0, 1, 1), std::invalid_argument);
    EXPECT_THROW(t.render(Image(a.data(), 4), Image(b.data(), Width * 4), &pixel,
                          0, 0, 1, 1), std::invalid_argument);
}
