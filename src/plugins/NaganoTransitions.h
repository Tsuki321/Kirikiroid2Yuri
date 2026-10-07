#pragma once

// Portable extNagano rendering. The API and recovered effect geometry are from
// https://github.com/wamsoft/extNagano/tree/4b403e2f684756c490975b9d1697289aa1f34240
// (a reconstruction, NOT the lost original source). See docs/EXTNAGANO.md for
// provenance and visual fidelity limitations.
// Images use the renderer's little-endian RGBA bytes (0xAABBGGRR integers).
#include "ExtraTransitions.h"
#include <limits>
#include <utility>

namespace NaganoTransitions {

using ExtraTransitions::Alpha;
using ExtraTransitions::Image;
using ExtraTransitions::blend;
using ExtraTransitions::clamp;
using ExtraTransitions::finiteRange;
constexpr double Pi = ExtraTransitions::Pi;

enum class Effect { Universal3D, BlurFade, ScanLine, ZoomFade, RGBFade, Spin,
                    Flutter, Book, ImageWipe, HoneyTurn, Morphing, MultiRipple };

struct Options {
    Effect effect = Effect::ScanLine;
    Alpha alpha = Alpha::Opaque;
    uint64_t time = 2;
    bool hsb = false;
    double speed1 = 0, accel1 = 0, speed2 = 0, accel2 = 0;
    int bound1 = 0, bound2 = 0;
    double exponent = 1;
    int blur1x = 0, blur1y = 0, blur2x = 0, blur2y = 0;
    int blurtype = 0, prerender = 0;
    double zoom1 = 100, zoom2 = 200;
    std::array<int, 4> delay = {{0, 0, 0, 0}}; // R, G, B, A
    int type1 = 0, type2 = 1;
    uint32_t back = 0;
    int backalpha = 255, slip = 8;
    int dir = 0, size = 40, order = 2;
    double twist = 0;
    std::vector<double> before, after;
    int count = 1, wavecount = 2, rwidth = 32, maxdrift = 24;
    double roundness = 1, delaylast = 1;
    uint32_t seed = 0x4e616761u; // The engine supplies per-transition randomness.
};

inline void validate(const Options &o, int width, int height) {
    if (width < 1 || height < 1 || width > 16384 || height > 16384 ||
        size_t(width) * size_t(height) > ExtraTransitions::MaxPixels)
        throw std::invalid_argument("extNagano image dimensions exceed the pixel limit");
    if (o.time < 2 || o.time > uint64_t(INT64_MAX))
        throw std::invalid_argument("extNagano time must be at least two milliseconds");
    switch (o.effect) {
    case Effect::Universal3D:
        if (!finiteRange(o.speed1, -1000000, 1000000) || !finiteRange(o.speed2, -1000000, 1000000) ||
            !finiteRange(o.accel1, -1000000, 1000000) || !finiteRange(o.accel2, -1000000, 1000000) ||
            o.bound1 < 0 || o.bound1 > 127 || o.bound2 < 0 || o.bound2 > 127)
            throw std::invalid_argument("invalid extNagano 3duniversal motion options");
        break;
    case Effect::BlurFade:
        if (!finiteRange(o.exponent, 0.01, 100) || o.blur1x < 0 || o.blur1x > 4096 ||
            o.blur1y < 0 || o.blur1y > 4096 || o.blur2x < 0 || o.blur2x > 4096 ||
            o.blur2y < 0 || o.blur2y > 4096 || o.blurtype < 0 || o.blurtype > 1 ||
            o.prerender < 0 || o.prerender > 2)
            throw std::invalid_argument("invalid extNagano blurfade options");
        break;
    case Effect::ZoomFade:
        if (!finiteRange(o.zoom1, 0.01, 100000) || !finiteRange(o.zoom2, 0.01, 100000))
            throw std::invalid_argument("extNagano zoom values must be finite positive percentages");
        break;
    case Effect::RGBFade:
        for (int d : o.delay) if (d < 0 || d > 255)
            throw std::invalid_argument("extNagano channel delays must be in 0..255");
        break;
    case Effect::Spin:
        if (o.type1 < -1 || o.type1 > 11 || o.type2 < -1 || o.type2 > 11)
            throw std::invalid_argument("extNagano spin types must be in -1..11");
        break;
    case Effect::Flutter:
        if (o.backalpha < 0 || o.backalpha > 255 || o.slip < 0 || o.slip > 16384)
            throw std::invalid_argument("invalid extNagano flutter options");
        break;
    case Effect::Book:
        if (o.dir < -1 || o.dir > 1) throw std::invalid_argument("extNagano book dir must be -1, 0 or 1");
        break;
    case Effect::ImageWipe:
        if (o.dir < 0 || o.dir > 1) throw std::invalid_argument("extNagano imagewipe dir must be 0 or 1");
        break;
    case Effect::HoneyTurn:
        if (o.size < 1 || o.size > 4096 || o.dir < 1 || o.dir > 9 ||
            o.order < 1 || o.order > 9 || !finiteRange(o.twist, -4096, 4096))
            throw std::invalid_argument("invalid extNagano honeyturn options");
        break;
    case Effect::Morphing:
        if (o.before.empty() || o.before.size() % 6 || o.before.size() != o.after.size() ||
            o.before.size() > 6 * 4096)
            throw std::invalid_argument("extNagano morphing before/after need equally sized triangle arrays");
        for (const auto *points : { &o.before, &o.after })
            for (double v : *points) if (!finiteRange(v, -32768, 32768))
                throw std::invalid_argument("extNagano morphing coordinates must be finite and bounded");
        break;
    case Effect::MultiRipple:
        if (o.count < 1 || o.count > 20 || o.wavecount < 1 || o.wavecount > 128 ||
            o.rwidth < 1 || o.rwidth > 4096 || o.maxdrift < 0 || o.maxdrift > 4096 ||
            !finiteRange(o.roundness, 0.01, 100) || !finiteRange(o.delaylast, 0, 100))
            throw std::invalid_argument("invalid extNagano multiripple options");
        break;
    case Effect::ScanLine: break;
    }
}

inline double unit(double v) { return std::max(0.0, std::min(1.0, v)); }
inline unsigned byte(uint32_t p, unsigned c) { return (p >> (c * 8)) & 255; }

// Composite rule decorations and moving fragments without turning transparent
// rules into opaque pixels. Additive layers already store premultiplied RGB.
inline uint32_t over(uint32_t bg, uint32_t fg, unsigned opacity, Alpha mode) {
    const unsigned fa = (fg >> 24) * opacity / 255;
    if (!fa) return bg;
    if (mode == Alpha::Opaque) return blend(bg, fg, int(fa), mode);
    const unsigned ba = bg >> 24, remain = 255 - fa;
    const unsigned oa = fa + (ba * remain + 127) / 255;
    uint32_t result = oa << 24;
    for (unsigned c = 0; c < 3; ++c) {
        unsigned v;
        if (mode == Alpha::Additive)
            v = (byte(fg, c) * opacity + byte(bg, c) * remain + 127) / 255;
        else
            v = (byte(fg, c) * fa * 255 + byte(bg, c) * ba * remain + oa * 127) / (oa * 255);
        result |= std::min(255u, v) << (c * 8);
    }
    return result;
}

class Transition {
    Options o;
    int width, height;
    uint64_t elapsed = 0;
    double progress = 0;
    bool prepared = false;
    std::vector<uint32_t> frame, scratch, blurred1, blurred2, rule;
    int ruleWidth = 0, ruleHeight = 0;
    std::vector<int> edges;
    struct Ripple { int x, y; double start, duration, distance; };
    std::vector<Ripple> ripples;

    uint32_t random() {
        o.seed = o.seed * 1664525u + 1013904223u;
        return o.seed;
    }
    uint32_t sample(const Image &a, double x, double y) const {
        // Clamp in floating point before conversion (extreme valid motion can
        // exceed int even when the image itself is small).
        const uint32_t value = a.get(int(std::max(0.0, std::min(double(width - 1), x))),
                                    int(std::max(0.0, std::min(double(height - 1), y))));
        return o.alpha == Alpha::Opaque ? value | 0xff000000u : value;
    }
    uint32_t shade(uint32_t p, int dark) const {
        uint32_t result = p & 0xff000000u;
        const int white = o.alpha == Alpha::Additive ? int(p >> 24) : 255;
        for (unsigned c = 0; c < 3; ++c) {
            int v = int(byte(p, c));
            v = dark >= 0 ? v * (255 - clamp(dark, 0, 255)) / 255 :
                v + (white - v) * clamp(-dark, 0, 255) / 255;
            result |= unsigned(v) << (c * 8);
        }
        return result;
    }
    void copy(const Image &a) {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) frame[size_t(y) * width + x] = a.get(x, y);
    }
    uint32_t premultiply(uint32_t p) const {
        if (o.alpha == Alpha::Opaque) return p | 0xff000000u;
        if (o.alpha != Alpha::Straight) return p;
        uint32_t result = p & 0xff000000u;
        for (unsigned c = 0; c < 3; ++c)
            result |= ((byte(p, c) * (p >> 24) + 127) / 255) << (c * 8);
        return result;
    }
    uint32_t unpremultiply(uint32_t p) const {
        if (o.alpha != Alpha::Straight) return p;
        const unsigned a = p >> 24;
        if (!a) return 0;
        uint32_t result = a << 24;
        for (unsigned c = 0; c < 3; ++c)
            result |= std::min(255u, (byte(p, c) * 255 + a / 2) / a) << (c * 8);
        return result;
    }
    void blur(const Image &src, std::vector<uint32_t> &out, int rx, int ry) {
        rx = std::min(rx, width - 1); ry = std::min(ry, height - 1);
        out.resize(frame.size()); scratch.resize(frame.size());
        if (o.blurtype == 1) {
            const int sx = rx * 2 + 1, sy = ry * 2 + 1;
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                const int x0 = (x / sx) * sx, x1 = std::min(width - 1, x0 + sx);
                scratch[size_t(y) * width + x] = blend(src.get(x0, y), src.get(x1, y),
                    x1 == x0 ? 0 : (x - x0) * 255 / (x1 - x0), o.alpha);
            }
            for (int y = 0; y < height; ++y) {
                const int y0 = (y / sy) * sy, y1 = std::min(height - 1, y0 + sy);
                const int ratio = y1 == y0 ? 0 : (y - y0) * 255 / (y1 - y0);
                for (int x = 0; x < width; ++x) out[size_t(y) * width + x] =
                    blend(scratch[size_t(y0) * width + x], scratch[size_t(y1) * width + x], ratio, o.alpha);
            }
            return;
        }
        // Sliding separable box filter: work is linear in image size, including
        // for large user-supplied radii. Clip edge windows instead of reading out.
        auto add = [](std::array<int64_t, 4> &sum, uint32_t p, int sign) {
            for (unsigned c = 0; c < 4; ++c) sum[c] += int64_t(byte(p, c)) * sign;
        };
        auto average = [](const std::array<int64_t, 4> &sum, int n) {
            uint32_t p = 0;
            for (unsigned c = 0; c < 4; ++c) p |= uint32_t((sum[c] + n / 2) / n) << (c * 8);
            return p;
        };
        for (int y = 0; y < height; ++y) {
            std::array<int64_t, 4> sum = {{0, 0, 0, 0}};
            int first = 0, end = 0;
            for (int x = 0; x < width; ++x) {
                const int nextEnd = std::min(width, x + rx + 1), nextFirst = std::max(0, x - rx);
                while (end < nextEnd) add(sum, premultiply(src.get(end++, y)), 1);
                while (first < nextFirst) add(sum, premultiply(src.get(first++, y)), -1);
                scratch[size_t(y) * width + x] = average(sum, end - first);
            }
        }
        for (int x = 0; x < width; ++x) {
            std::array<int64_t, 4> sum = {{0, 0, 0, 0}};
            int first = 0, end = 0;
            for (int y = 0; y < height; ++y) {
                const int nextEnd = std::min(height, y + ry + 1), nextFirst = std::max(0, y - ry);
                while (end < nextEnd) add(sum, scratch[size_t(end++) * width + x], 1);
                while (first < nextFirst) add(sum, scratch[size_t(first++) * width + x], -1);
                out[size_t(y) * width + x] = unpremultiply(average(sum, end - first));
            }
        }
    }
    void renderBlur(const Image &a, const Image &b) {
        const double p = std::pow(progress, o.exponent);
        blur(a, blurred1, int(o.blur1x * p + 0.5), int(o.blur1y * p + 0.5));
        blur(b, blurred2, int(o.blur2x * (1 - p) + 0.5), int(o.blur2y * (1 - p) + 0.5));
        for (size_t i = 0; i < frame.size(); ++i)
            frame[i] = blend(blurred1[i], blurred2[i], int(p * 255 + 0.5), o.alpha);
    }
    void scatter(const Image &src, bool second) {
        const int phase = int(progress * 255), motionPhase = second ? 255 - phase : phase;
        const double speed = second ? o.speed2 : o.speed1, accel = second ? o.accel2 : o.accel1;
        const int bound = second ? o.bound2 : o.bound1;
        std::array<double, 256> distance, cosine, sine;
        for (int i = 0; i < 256; ++i) {
            const int t = bound ? i % (255 - bound) : i;
            distance[i] = std::round(0.5 * accel * t * t + speed * t);
            cosine[i] = std::round(std::cos(i * 2 * Pi / 255) * 1024);
            sine[i] = std::round(std::sin(i * 2 * Pi / 255) * 1024);
        }
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const uint32_t r = rule[size_t(std::min(y, ruleHeight - 1)) * ruleWidth + std::min(x, ruleWidth - 1)];
            const int start = byte(r, 0), velocity = byte(r, 1), direction = byte(r, 2);
            const int t = motionPhase - start;
            if (t < 0) { frame[size_t(y) * width + x] = src.get(x, y); continue; }
            const double dx = std::floor(cosine[direction] * velocity * distance[t] / 524288.0);
            const double dy = std::floor(sine[direction] * velocity * distance[t] / 524288.0);
            const double tx = x + dx, ty = y + dy;
            if (tx < 0 || ty < 0 || tx >= width || ty >= height) continue;
            const int opacity = clamp(t * 255 / (256 - start), 0, 255);
            auto &dest = frame[size_t(int(ty)) * width + int(tx)];
            const uint32_t pixel = src.get(x, y);
            dest = over(dest, o.alpha == Alpha::Opaque ? pixel | 0xff000000u : pixel,
                        unsigned(opacity), o.alpha);
        }
    }
    void renderUniversal(const Image &a, const Image &b) {
        if (o.speed2 == 0 && o.accel2 == 0) { copy(b); scatter(a, false); }
        else if (o.speed1 == 0 && o.accel1 == 0) { copy(a); scatter(b, true); }
        else {
            std::fill(frame.begin(), frame.end(), o.alpha == Alpha::Opaque ? 0xff000000u : 0u);
            scatter(a, false); scatter(b, true);
        }
    }
    uint32_t rgb(uint32_t a, uint32_t b) const {
        const int maxDelay = *std::max_element(o.delay.begin(), o.delay.end());
        const double duration = std::max(1.0 / double(o.time), (255.0 - maxDelay) / 255.0);
        std::array<unsigned, 4> ratios;
        for (unsigned c = 0; c < 4; ++c)
            ratios[c] = unsigned(unit((progress - o.delay[c] / 255.0) / duration) * 255);
        uint32_t result = 0;
        for (unsigned c = 0; c < 4; ++c) {
            const unsigned r = ratios[c];
            unsigned v;
            if (c == 3 && o.alpha == Alpha::Opaque) v = 255;
            else if (c < 3 && o.alpha == Alpha::Straight) {
                const unsigned wa = (a >> 24) * (255 - r), wb = (b >> 24) * r;
                v = wa + wb ? (byte(a, c) * wa + byte(b, c) * wb) / (wa + wb) : 0;
            } else v = (byte(a, c) * (255 - r) + byte(b, c) * r + 127) / 255;
            result |= v << (c * 8);
        }
        return result;
    }
    struct SpinColumn {
        double x, slope;
        SpinColumn(double column = -1, double scale = 1) : x(column), slope(scale) {}
    };
    SpinColumn spinColumn(int x, int type, bool second) const {
        if (type == -1) return { double(x), 1 };
        const double axis = type == 4 || type == 5 ? 0.0 : type == 6 || type == 7 ? double(width) : width * 0.5;
        const double sign = (type & 1) ? -1.0 : 1.0;
        const double angle = second ? -sign * (progress - 1) * Pi * 0.5 : sign * progress * Pi * 0.5;
        const double d = width * 2.0, offset = x - axis;
        const double denominator = d * std::cos(angle) - offset * std::sin(angle);
        if (std::abs(denominator) < 1e-9) return {};
        const double sx = offset * d / denominator;
        const double slope = (sx * std::sin(angle) + d) / d;
        if (slope <= 0 || sx + axis < 0 || sx + axis >= width) return {};
        return {sx + axis, slope};
    }
    uint32_t book(const Image &a, const Image &b, int x, int y) const {
        const int p = int((width / 2) * progress * progress);
        if (!p) return a.get(x, y);
        const bool mirror = o.dir == 1;
        const int c = mirror ? width - 1 - x : x;
        const int border = std::min(p, 16);
        const int b1 = width - 2 * p - border, b2 = width - 2 * p;
        const int b3 = width - 3 * p / 2, b4 = width - p;
        const int q = int(std::sqrt(std::max(0.0, double(width / 2 - p) * 32768 / width)) + 0.5);
        if (c < b1) return a.get(x, y);
        if (c < b2) return shade(a.get(x, y), (c - b1) * q / std::max(1, border) / 2);
        if (c >= b4) return shade(b.get(x, y), (width - c) * q / p);
        const int sourceColumn = c + 2 * p - width;
        const uint32_t incoming = sample(b, mirror ? width - 1 - sourceColumn : sourceColumn, y);
        const int darkness = c < b3 ? -(b3 - c) * q * 2 / p : ((c - b4) * 2 + p) * q / p;
        return over(shade(a.get(x, y), q / 2), shade(incoming, darkness), 255, o.alpha);
    }
    uint32_t flutter(const Image &a, const Image &b, int x, int y) const {
        // A diagonal fold, with reflected back, slip, highlight and contact
        // shadow. Absolute coordinates keep divided updates seam-free.
        const double shift = std::min({ o.slip, width, height }) * std::sin(progress * Pi);
        const double fold = (width + height) * (1 - progress * progress);
        if (x < shift || y < shift) return b.get(x, y);
        const double side = x + y - fold;
        if (side <= 0) {
            const uint32_t p = sample(a, x - shift, y - shift);
            return side > -8 ? shade(p, int((side + 8) * 7)) : p;
        }
        const double band = std::min(width, height) * 0.3 * std::sin(progress * Pi);
        if (side >= band || band < 0.5) return b.get(x, y);
        uint32_t back = sample(a, fold - y - shift, fold - x - shift);
        if (o.back >> 24) back = blend(back, o.back | 0xff000000u, int(o.back >> 24), o.alpha);
        back = shade(back, int(80 * side / band - 40 * std::sin(side / band * Pi)));
        return over(b.get(x, y), back, unsigned(o.backalpha), o.alpha);
    }
    uint32_t wipe(const Image &a, const Image &b, int x, int y) const {
        const int origin = int((width + ruleWidth) * (o.dir ? 1 - progress : progress)) - ruleWidth;
        const int ry = std::min(y, ruleHeight - 1), edge = edges[ry];
        const bool left = x < origin + edge;
        uint32_t result = (o.dir ? !left : left) ? b.get(x, y) : a.get(x, y);
        const int rx = x - origin;
        if (rx >= 0 && rx < edge)
            result = over(result, rule[size_t(ry) * ruleWidth + rx], 255, o.alpha);
        return result;
    }
    uint32_t honey(const Image &a, const Image &b, int x, int y) const {
        // Locate the nearest staggered hexagon centre (flat-sided hex cells).
        const double rowStep = o.size * 1.5, colStep = o.size * std::sqrt(3.0);
        const int guess = int(std::floor(y / rowStep));
        double cx = 0, cy = 0, best = std::numeric_limits<double>::max();
        for (int row = guess - 1; row <= guess + 1; ++row) {
            const double offset = (row & 1) ? colStep * 0.5 : 0;
            const double centerX = std::round((x - offset) / colStep) * colStep + offset;
            const double centerY = row * rowStep, dx = x - centerX, dy = y - centerY;
            const double d = dx * dx + dy * dy;
            if (d < best) { best = d; cx = centerX; cy = centerY; }
        }
        const int ox = (o.order - 1) % 3 - 1, oy = 1 - (o.order - 1) / 3;
        double order;
        if (o.order == 5) order = unit(std::hypot(cx - width * 0.5, cy - height * 0.5) /
                                       std::max(1.0, std::hypot(width * 0.5, height * 0.5)));
        else order = unit((ox * (cx / width - 0.5) + oy * (cy / height - 0.5)) /
                           std::max(1, std::abs(ox) + std::abs(oy)) + 0.5);
        const double p = unit(progress * 2 - order);
        if (p <= 0) return a.get(x, y);
        if (p >= 1) return b.get(x, y);
        const double vis = std::abs(1 - 2 * p);
        const double half = colStep * 0.5 * std::min(1.0, std::max(0.0, 2 - 2 * std::abs(y - cy) / o.size));
        const double rel = x - cx;
        if (vis < 1e-6 || std::abs(rel) > half * vis) return o.alpha == Alpha::Opaque ? 0xff000000u : 0u;
        const double direction = o.dir == 1 || o.dir == 4 || o.dir == 7 ? -1.0 : 1.0;
        const double sx = cx + direction * rel / vis + o.twist / o.size * (y - cy) * (1 - vis);
        if (sx < 0 || sx >= width) return o.alpha == Alpha::Opaque ? 0xff000000u : 0u;
        return sample(p < 0.5 ? a : b, sx, y);
    }
    void setupRipples() {
        ripples.resize(o.count);
        const double travel = double(o.rwidth) * o.wavecount;
        for (int i = 0; i < o.count; ++i) {
            auto &r = ripples[i];
            r.x = i == o.count - 1 ? width / 2 : int(random() % unsigned(width));
            r.y = i == o.count - 1 ? height / 2 : int(random() % unsigned(height));
            r.distance = std::hypot(std::max(r.x, width - 1 - r.x),
                                   std::max(r.y, height - 1 - r.y) * o.roundness) + travel;
            // Spread wave starts over the first half. Delay affects the centre
            // while every wave is guaranteed to finish by the requested time.
            r.start = o.count == 1 ? 0 : (double(i) / (o.count - 1)) * 0.5;
            if (i == o.count - 1 && o.count > 1) r.start = std::min(0.95, r.start * o.delaylast);
            r.duration = 1 - r.start;
        }
    }
    uint32_t ripple(const Image &a, const Image &b, int x, int y) const {
        const double travel = double(o.rwidth) * o.wavecount;
        double phase = 0, displacement = 0;
        for (const auto &r : ripples) {
            const double p = unit((progress - r.start) / r.duration);
            const double distance = std::hypot(x - r.x, (y - r.y) * o.roundness);
            const double behind = p * r.distance - distance;
            if (behind <= 0) continue;
            const double wave = unit(behind / travel);
            phase += wave * (765 / o.count);
            if (wave < 1) displacement -= std::sin(behind * 2 * Pi / o.rwidth) *
                (1 - wave) * o.maxdrift * (1 - p);
        }
        const int mix = int(phase), ratio = mix & 255;
        // The recovered plugin reverses the blend on each 256-phase boundary;
        // a passing wave can expose both images before settling on the new one.
        return blend(sample((mix & 256) ? b : a, x, y + displacement),
                     sample((mix & 256) ? a : b, x, y + displacement), ratio, o.alpha);
    }
    void morph(const Image &a, const Image &b) {
        const int ratio = int(progress * 255);
        // Uncovered patches have a defined background; malformed arrays are
        // rejected rather than silently replacing the entire morph with a fade.
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
            frame[size_t(y) * width + x] = blend(a.get(x, y), b.get(x, y), ratio, o.alpha);
        uint64_t rasterWork = 0;
        for (size_t i = 0; i < o.before.size(); i += 6) {
            std::array<double, 6> p;
            for (unsigned k = 0; k < 6; ++k) p[k] = o.before[i + k] * (1 - progress) + o.after[i + k] * progress;
            const double det = (p[3] - p[5]) * (p[0] - p[4]) + (p[4] - p[2]) * (p[1] - p[5]);
            if (std::abs(det) < 1e-9) continue; // A triangle may collapse during a legal morph.
            const int left = clamp(int(std::floor(std::min({ p[0], p[2], p[4] }))), 0, width);
            const int right = clamp(int(std::ceil(std::max({ p[0], p[2], p[4] }))), 0, width);
            const int top = clamp(int(std::floor(std::min({ p[1], p[3], p[5] }))), 0, height);
            const int bottom = clamp(int(std::ceil(std::max({ p[1], p[3], p[5] }))), 0, height);
            rasterWork += uint64_t(right - left) * (bottom - top);
            if (rasterWork > 134217728)
                throw std::invalid_argument("extNagano morphing triangles exceed the frame work limit");
            for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
                const double u = ((p[3] - p[5]) * (x + 0.5 - p[4]) + (p[4] - p[2]) * (y + 0.5 - p[5])) / det;
                const double v = ((p[5] - p[1]) * (x + 0.5 - p[4]) + (p[0] - p[4]) * (y + 0.5 - p[5])) / det;
                const double w = 1 - u - v;
                if (u < -1e-9 || v < -1e-9 || w < -1e-9) continue;
                auto coordinate = [&](const std::vector<double> &points, int k) {
                    return u * points[i + k] + v * points[i + 2 + k] + w * points[i + 4 + k] - 0.5 + 1e-9;
                };
                frame[size_t(y) * width + x] = blend(sample(a, coordinate(o.before, 0), coordinate(o.before, 1)),
                    sample(b, coordinate(o.after, 0), coordinate(o.after, 1)), ratio, o.alpha);
            }
        }
    }
    void prepare(const Image &a, const Image &b) {
        frame.resize(size_t(width) * height);
        if (!elapsed) copy(a);
        else if (elapsed == o.time) copy(b);
        else if (o.effect == Effect::Universal3D) renderUniversal(a, b);
        else if (o.effect == Effect::BlurFade) renderBlur(a, b);
        else if (o.effect == Effect::Morphing) morph(a, b);
        else {
            std::vector<SpinColumn> col1, col2;
            if (o.effect == Effect::Spin) {
                col1.resize(width); col2.resize(width);
                for (int x = 0; x < width; ++x) {
                    col1[x] = spinColumn(x, o.type1, false); col2[x] = spinColumn(x, o.type2, true);
                }
            }
            const int ratio = int(progress * 255), slide = int(progress * width);
            const double zoom1 = 1 + (o.zoom1 / 100 - 1) * progress;
            const double zoom2 = o.zoom2 / 100 + (1 - o.zoom2 / 100) * progress;
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                uint32_t p = 0;
                switch (o.effect) {
                case Effect::ScanLine:
                    if (!(y & 1)) p = x < slide ? b.get(x + width - slide, y) : a.get(x - slide, y);
                    else p = x >= width - slide ? b.get(x - width + slide, y) : a.get(x + slide, y);
                    break;
                case Effect::ZoomFade: {
                    const double dx = x - width * 0.5, dy = y - height * 0.5;
                    p = blend(sample(a, width * 0.5 + dx / zoom1, height * 0.5 + dy / zoom1),
                              sample(b, width * 0.5 + dx / zoom2, height * 0.5 + dy / zoom2), ratio, o.alpha);
                    break;
                }
                case Effect::RGBFade: p = rgb(a.get(x, y), b.get(x, y)); break;
                case Effect::Spin: {
                    const bool first = col1[x].x >= 0, second = col2[x].x >= 0;
                    const bool useSecond = second && (!first || col2[x].slope < col1[x].slope);
                    const auto &col = useSecond ? col2[x] : col1[x];
                    const double sy = height * 0.5 + (y - height * 0.5) * col.slope;
                    p = (!first && !second) || sy < 0 || sy >= height ?
                        (o.alpha == Alpha::Opaque ? 0xff000000u : 0u) : sample(useSecond ? b : a, col.x, sy);
                    break;
                }
                case Effect::Flutter: p = flutter(a, b, x, y); break;
                case Effect::Book: p = book(a, b, x, y); break;
                case Effect::ImageWipe: p = wipe(a, b, x, y); break;
                case Effect::HoneyTurn: p = honey(a, b, x, y); break;
                case Effect::MultiRipple: p = ripple(a, b, x, y); break;
                default: throw std::logic_error("extNagano effect has no renderer");
                }
                frame[size_t(y) * width + x] = p;
            }
        }
        prepared = true;
    }
public:
    Transition(Options options, int w, int h, std::vector<uint32_t> rulePixels = {}, int rw = 0, int rh = 0)
        : o(std::move(options)), width(w), height(h), rule(std::move(rulePixels)), ruleWidth(rw), ruleHeight(rh) {
        validate(o, width, height);
        if (o.effect == Effect::Universal3D || o.effect == Effect::ImageWipe) {
            if (rw < 1 || rh < 1 || rw > 16384 || rh > 16384 ||
                size_t(rw) * rh > ExtraTransitions::MaxPixels || rule.size() != size_t(rw) * rh)
                throw std::invalid_argument("extNagano transition requires a valid RGBA rule image");
            if (o.effect == Effect::Universal3D && o.hsb) {
                for (auto &p : rule) {
                    const int red = byte(p, 0), green = byte(p, 1), blue = byte(p, 2);
                    const int high = std::max({red, green, blue}), low = std::min({red, green, blue});
                    const int chroma = high - low;
                    int hue = 0;
                    if (chroma) {
                        if (high == green) hue = 120 + (blue - red) * 60 / chroma;
                        else if (high == blue) hue = 240 + (red - green) * 60 / chroma;
                        else if (green < blue) hue = 360 + (green - blue) * 60 / chroma;
                        else hue = (green - blue) * 60 / chroma;
                        if (hue < 0) hue += 360;
                    }
                    // Recovered DLL inverts brightness and doubles saturation;
                    // its integer hue/saturation rounding affects pixel motion.
                    const unsigned speed = high ? std::min(255, (chroma * 255 / high) * 2) : 0;
                    p = unsigned(255 - high) | (speed << 8) | (unsigned(hue * 255 / 360) << 16);
                }
            }
            if (o.effect == Effect::ImageWipe) {
                // Rules describe straight-alpha decorations independently of
                // the transition layer's source-image representation.
                if (o.alpha == Alpha::Additive) for (auto &p : rule) {
                    const unsigned alpha = p >> 24;
                    uint32_t converted = p & 0xff000000u;
                    for (unsigned c = 0; c < 3; ++c)
                        converted |= ((byte(p, c) * alpha + 127) / 255) << (c * 8);
                    p = converted;
                }
                edges.resize(rh, rw / 2);
                for (int y = 0; y < rh; ++y) {
                    for (int x = 0; x < rw; ++x) if ((rule[size_t(y) * rw + x] >> 24) > 240) edges[y] = x;
                }
            }
        }
        if (o.effect == Effect::Book && o.dir < 0) o.dir = int(random() & 1);
        if (o.effect == Effect::MultiRipple) setupRipples();
    }
    void setTime(uint64_t time) {
        elapsed = std::min(time, o.time); progress = double(elapsed) / double(o.time);
        prepared = false; // Sources may change even if two clock ticks coincide.
    }
    void render(const Image &a, const Image &b, uint32_t *dest, int left, int top, int w, int h) {
        const ptrdiff_t stride = ptrdiff_t(width) * 4;
        if (!a.pixels || !b.pixels || !dest || left < 0 || top < 0 || w < 0 || h < 0 ||
            left > width || top > height || w > width - left || h > height - top ||
            (a.pitch < stride && a.pitch > -stride) || (b.pitch < stride && b.pitch > -stride))
            throw std::invalid_argument("invalid extNagano image region or stride");
        if (!w || !h) return;
        if (!prepared) prepare(a, b);
        for (int y = 0; y < h; ++y)
            std::memcpy(dest + size_t(y) * w, frame.data() + size_t(top + y) * width + left, size_t(w) * 4);
    }
};

} // namespace NaganoTransitions
