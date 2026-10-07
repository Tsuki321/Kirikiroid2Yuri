#include <gtest/gtest.h>
#include "ExtraTransitions.h"
#include <limits>

using namespace ExtraTransitions;

namespace {
constexpr int Width = 64, Height = 48;
const Effect Effects[] = { Effect::Mosaic, Effect::Wave, Effect::Ripple, Effect::RotateSwap,
                          Effect::Turn, Effect::RotateZoom, Effect::RotateVanish };
Options options(Effect effect) {
    Options o;
    o.effect = effect; o.time = 1000;
    o.centerx = Width / 2; o.centery = Height / 2;
    return o;
}
std::vector<uint32_t> picture(bool second) {
    std::vector<uint32_t> p(Width * Height);
    for (int y = 0; y < Height; ++y)
        for (int x = 0; x < Width; ++x)
            p[y * Width + x] = 0xff000000u | (second ? 0x800000 : 0) | (x << 8) | y;
    return p;
}
std::vector<uint32_t> render(Transition &t, uint64_t time,
                             const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
    std::vector<uint32_t> out(Width * Height);
    t.setTime(time);
    t.render(Image(a.data(), Width * 4), Image(b.data(), Width * 4), out.data(), 0, 0, Width, Height);
    return out;
}
}

TEST(ExtraTransitions, EveryEffectHasExactEndpointsAndClampsLateTicks) {
    const auto a = picture(false), b = picture(true);
    for (auto effect : Effects) {
        Transition t(options(effect), Width, Height);
        EXPECT_EQ(render(t, 0, a, b), a);
        EXPECT_EQ(render(t, 1000, a, b), b);
        EXPECT_EQ(render(t, std::numeric_limits<uint64_t>::max(), a, b), b);
    }
}

TEST(ExtraTransitions, DividedNonzeroRegionsEqualTheFullFrame) {
    const auto a = picture(false), b = picture(true);
    for (auto effect : Effects) {
        Transition t(options(effect), Width, Height);
        for (uint64_t time : { 1u, 251u, 500u, 999u }) {
            auto full = render(t, time, a, b);
            for (int top = 0; top < Height; top += 7) for (int left = 0; left < Width; left += 11) {
                const int w = std::min(11, Width - left), h = std::min(7, Height - top);
                std::vector<uint32_t> patch(w * h + 2, 0x12345678);
                t.render(Image(a.data(), Width * 4), Image(b.data(), Width * 4), patch.data() + 1, left, top, w, h);
                EXPECT_EQ(patch.front(), 0x12345678u);
                EXPECT_EQ(patch.back(), 0x12345678u);
                for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
                    ASSERT_EQ(patch[1 + y * w + x], full[(top + y) * Width + left + x]);
            }
        }
    }
}

TEST(ExtraTransitions, ReadsIndependentPaddedAndNegativeSourceStrides) {
    const auto a = picture(false), b = picture(true);
    std::vector<uint32_t> padded((Width + 3) * Height), flipped((Width + 5) * Height);
    for (int y = 0; y < Height; ++y) {
        std::copy_n(a.data() + y * Width, Width, padded.data() + y * (Width + 3));
        std::copy_n(b.data() + y * Width, Width, flipped.data() + (Height - y - 1) * (Width + 5));
    }
    for (auto effect : Effects) {
        Transition t(options(effect), Width, Height);
        const auto expected = render(t, 500, a, b);
        std::vector<uint32_t> actual(Width * Height);
        t.render(Image(padded.data(), (Width + 3) * 4),
                 Image(flipped.data() + (Height - 1) * (Width + 5), -(Width + 5) * 4),
                 actual.data(), 0, 0, Width, Height);
        EXPECT_EQ(actual, expected);
    }
}

TEST(ExtraTransitions, MosaicSamplesBlockCentersAndSupportsBlocksLargerThanTheImage) {
    auto o = options(Effect::Mosaic); o.maxsize = 8;
    Transition t(o, Width, Height);
    const auto a = picture(false), b = picture(true);
    const auto out = render(t, 500, a, b);
    // At half time an eight-pixel grid is centered on the image. This block
    // spans x=4..11, y=4..11 and samples the source at (8,8).
    const uint32_t expected = blend(a[8 * Width + 8], b[8 * Width + 8], 127, Alpha::Opaque);
    for (int y = 4; y < 12; ++y) for (int x = 4; x < 12; ++x)
        EXPECT_EQ(out[y * Width + x], expected);
    o.maxsize = 32768;
    Transition large(o, Width, Height);
    EXPECT_EQ(render(large, 500, a, b).size(), a.size());
}

TEST(ExtraTransitions, WaveUsesItsDisplacedSourceAndBackgroundColors) {
    auto o = options(Effect::Wave);
    o.maxh = 8; o.maxomega = Pi / 2; o.bgcolor1 = o.bgcolor2 = 0xff102030;
    Transition t(o, Width, Height);
    const auto a = picture(false), b = picture(true), out = render(t, 500, a, b);
    // One row below center, sin(pi/2) displaces the image eight pixels right.
    EXPECT_EQ(out[(Height / 2 + 1) * Width], 0xff102030u);
    EXPECT_EQ(out[(Height / 2 + 1) * Width + 20],
              blend(a[(Height / 2 + 1) * Width + 12], b[(Height / 2 + 1) * Width + 12], 127, Alpha::Opaque));
}

TEST(ExtraTransitions, WaveFrequencyModesProduceDifferentGeometry) {
    const auto a = picture(false), b = picture(true);
    auto o = options(Effect::Wave); o.maxh = 12;
    Transition normal(o, Width, Height);
    o.wavetype = 1; Transition rising(o, Width, Height);
    o.wavetype = 2; Transition falling(o, Width, Height);
    EXPECT_NE(render(normal, 250, a, b), render(rising, 250, a, b));
    EXPECT_NE(render(rising, 250, a, b), render(falling, 250, a, b));
}

TEST(ExtraTransitions, ZeroRippleDriftBlendsInPlaceAndDriftChangesGeometry) {
    const auto a = picture(false), b = picture(true);
    auto o = options(Effect::Ripple); o.maxdrift = 0;
    Transition still(o, Width, Height);
    const auto out = render(still, 500, a, b);
    for (size_t i = 0; i < out.size(); ++i) EXPECT_EQ(out[i], blend(a[i], b[i], 127, Alpha::Opaque));
    o.maxdrift = 12; Transition waves(o, Width, Height);
    EXPECT_NE(render(waves, 500, a, b), out);
    o.roundness = 2; Transition ellipse(o, Width, Height);
    EXPECT_NE(render(waves, 500, a, b), render(ellipse, 500, a, b));
}

TEST(ExtraTransitions, RippleReflectsImageEdgesAndAcceptsLegacyWavelengths) {
    const auto a = picture(false), b = picture(true);
    for (int rwidth : {16, 32, 64, 128}) for (int corner = 0; corner < 4; ++corner) {
        auto o = options(Effect::Ripple); o.rwidth = rwidth;
        o.centerx = (corner & 1) ? Width - 1 : 0;
        o.centery = (corner & 2) ? Height - 1 : 0;
        o.maxdrift = Height - 1; o.roundness = 0.000001;
        Transition t(o, Width, Height);
        const auto out = render(t, 500, a, b);
        for (auto pixel : out) {
            EXPECT_LT(pixel & 255, unsigned(Height));
            EXPECT_LT((pixel >> 8) & 255, unsigned(Width));
        }
    }
}

TEST(ExtraTransitions, RotateSwapDrawsBothMovingImagesAndBackground) {
    auto o = options(Effect::RotateSwap); o.twist = 0; o.bgcolor = 0xff123456;
    Transition t(o, Width, Height);
    std::vector<uint32_t> a(Width * Height, 0xffff0000), b(Width * Height, 0xff00ff00);
    const auto out = render(t, 500, a, b);
    EXPECT_GT(std::count(out.begin(), out.end(), 0xffff0000), 0);
    EXPECT_GT(std::count(out.begin(), out.end(), 0xff00ff00), 0);
    EXPECT_GT(std::count(out.begin(), out.end(), o.bgcolor), 0);
    o.twist = 0.5; Transition rotated(o, Width, Height);
    EXPECT_NE(render(rotated, 500, a, b), out);
}

TEST(ExtraTransitions, AlphaInterpolationDoesNotLeakTransparentColors) {
    const uint32_t opaqueRed = 0xffff0000, transparentBlue = 0x000000ff;
    const auto straight = blend(opaqueRed, transparentBlue, 127, Alpha::Straight);
    EXPECT_EQ(straight, 0x80ff0000u);
    EXPECT_EQ(blend(0x00ffffff, transparentBlue, 127, Alpha::Straight), 0u);
    EXPECT_EQ(blend(0x80800000, 0x00000000, 127, Alpha::Additive), 0x40400000u);
    EXPECT_EQ(blend(opaqueRed, transparentBlue, 0, Alpha::Straight), opaqueRed);
    EXPECT_EQ(blend(opaqueRed, transparentBlue, 255, Alpha::Straight), transparentBlue);
}

TEST(ExtraTransitions, TurnMatchesOriginalTableSamplesAndClipsPartialTiles) {
    const auto a = picture(false), b = picture(true);
    auto o = options(Effect::Turn); o.bgcolor = 0xff234567;
    Transition t(o, Width, Height);
    // Golden samples from W.Dee's turntrans_table.cpp, phases 8, 16 and 32.
    // The 48-pixel image height also exercises the clipped final tile.
    auto out = render(t, 148, a, b);
    EXPECT_EQ(out[24 * Width], 0xffbfbfc5u); // phase 8's 192/256 gloss
    out = render(t, 265, a, b);
    EXPECT_EQ(out[24 * Width + 4], 0xff00011bu);
    EXPECT_EQ(out[24 * Width + 2], o.bgcolor);
    out = render(t, 500, a, b);
    EXPECT_EQ(out[24 * Width + 23], 0xff80002bu);
    EXPECT_EQ(out[24 * Width + 24], 0xff803600u);
    EXPECT_EQ(out[24 * Width + 25], o.bgcolor);
    EXPECT_EQ(out[47 * Width + 46], o.bgcolor); // source y=63 exceeds this image
}

TEST(ExtraTransitions, TurnStaggersTilesFromBottomLeftToTopRight) {
    constexpr int w = 192, h = 128;
    const std::vector<uint32_t> a(w * h, 0xffff0000), b(w * h, 0xff00ff00);
    std::vector<uint32_t> out(w * h);
    auto o = options(Effect::Turn); o.bgcolor = 0xff0000ff;
    Transition t(o, w, h);
    t.setTime(500);
    t.render(Image(a.data(), w * 4), Image(b.data(), w * 4), out.data(), 0, 0, w, h);
    EXPECT_EQ(out[96 * w + 32], b.front());
    EXPECT_EQ(out[32 * w + 160], a.front());
    EXPECT_EQ(out[90 * w + 4], o.bgcolor);
}

TEST(ExtraTransitions, TurnGlossPreservesStraightAndAdditiveAlpha) {
    for (Alpha alpha : { Alpha::Straight, Alpha::Additive }) {
        const uint32_t color = alpha == Alpha::Straight ? 0x800000ffu : 0x80000080u;
        const std::vector<uint32_t> a(Width * Height, color), b(Width * Height, 0);
        auto o = options(Effect::Turn); o.alpha = alpha;
        Transition t(o, Width, Height);
        const uint32_t pixel = render(t, 148, a, b)[24 * Width];
        EXPECT_EQ(pixel >> 24, 128u);
        EXPECT_EQ(pixel & 255u, color & 255u);
        EXPECT_EQ((pixel >> 8) & 255u, alpha == Alpha::Straight ? 191u : 96u);
    }
}

TEST(ExtraTransitions, RotateZoomAndVanishUseOppositeForegroundImages) {
    const auto a = picture(false), b = picture(true);
    auto o = options(Effect::RotateZoom); o.factor = 0; o.twist = 0;
    Transition zoom(o, Width, Height);
    auto out = render(zoom, 500, a, b);
    EXPECT_EQ(out.front(), a.front());
    EXPECT_EQ(out[12 * Width + 16], b.front());
    EXPECT_EQ(out[30 * Width + 40], b[36 * Width + 48]);
    EXPECT_NE(out, a); EXPECT_NE(out, b);

    o.effect = Effect::RotateVanish;
    Transition vanish(o, Width, Height);
    out = render(vanish, 500, a, b);
    EXPECT_EQ(out.front(), b.front());
    EXPECT_EQ(out[12 * Width + 16], a.front());
    EXPECT_EQ(out[30 * Width + 40], a[36 * Width + 48]);
    EXPECT_NE(out, a); EXPECT_NE(out, b);
}

TEST(ExtraTransitions, RotateZoomHonorsCenterAccelerationAndTwist) {
    const auto a = picture(false), b = picture(true);
    auto o = options(Effect::RotateZoom); o.factor = 0; o.twist = 0;
    o.accel = -2;
    Transition decelerating(o, Width, Height);
    EXPECT_EQ(render(decelerating, 500, a, b)[6 * Width + 8], b.front());
    o.accel = 2;
    Transition accelerating(o, Width, Height);
    EXPECT_EQ(render(accelerating, 500, a, b)[12 * Width + 16], a[12 * Width + 16]);
    o.accel = 0; o.centerx = 0; o.centery = 0;
    Transition corner(o, Width, Height);
    EXPECT_EQ(render(corner, 500, a, b)[24 * Width + 32], b[12 * Width + 16]);
    o.centerx = Width / 2; o.centery = Height / 2; o.twist = 0.5;
    Transition rotation(o, Width, Height);
    EXPECT_EQ(render(rotation, 500, a, b)[24 * Width + 40], b[40 * Width + 32]);
    o.twistaccel = 2;
    Transition acceleratedRotation(o, Width, Height);
    EXPECT_NE(render(rotation, 500, a, b), render(acceleratedRotation, 500, a, b));
}

TEST(ExtraTransitions, RejectsInvalidDimensionsRegionsAndNonfiniteOptions) {
    auto o = options(Effect::Mosaic);
    EXPECT_THROW(Transition(o, 0, Height), std::invalid_argument);
    EXPECT_THROW(Transition(o, 16384, 16384), std::invalid_argument);
    o.maxsize = 0; EXPECT_THROW(Transition(o, Width, Height), std::invalid_argument);
    o = options(Effect::Wave); o.maxomega = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(Transition(o, Width, Height), std::invalid_argument);
    o = options(Effect::Ripple); o.maxdrift = 128;
    EXPECT_THROW(Transition(o, Width, Height), std::invalid_argument);
    o.maxdrift = 0; o.rwidth = 31;
    EXPECT_THROW(Transition(o, Width, Height), std::invalid_argument);
    o.rwidth = 16; o.centerx = Width;
    EXPECT_THROW(Transition(o, Width, Height), std::invalid_argument);
    o = options(Effect::RotateSwap); o.twist = std::numeric_limits<double>::infinity();
    EXPECT_THROW(Transition(o, Width, Height), std::invalid_argument);
    o = options(Effect::RotateZoom); o.factor = std::numeric_limits<double>::infinity();
    EXPECT_THROW(Transition(o, Width, Height), std::invalid_argument);
    o = options(Effect::RotateVanish); o.accel = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(Transition(o, Width, Height), std::invalid_argument);
    o = options(Effect::Mosaic); Transition t(o, Width, Height);
    const auto a = picture(false), b = picture(true);
    uint32_t out;
    EXPECT_THROW(t.render(Image(a.data(), Width * 4), Image(b.data(), Width * 4), &out,
                          Width, 0, 1, 1), std::invalid_argument);
    EXPECT_THROW(t.render(Image(a.data(), 4), Image(b.data(), Width * 4), &out,
                          0, 0, 1, 1), std::invalid_argument);
}
