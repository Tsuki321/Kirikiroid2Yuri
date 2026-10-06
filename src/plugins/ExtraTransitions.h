#pragma once

// Portable implementations of the extrans effects used by KAG games.
// Timing, option names and effect geometry follow W.Dee's Kirikiri2 extrans:
// https://github.com/krkrz/krkr2/tree/ea6b8260ac6a8d76bbe91fdb9e7d08fa009125bf/kirikiri2/trunk/kirikiri2/src/plugins/win32/extrans
// Pixel access is independent of the renderer and does not require Win32/MMX.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace ExtraTransitions {

enum class Effect { Mosaic, Wave, Ripple, RotateSwap };
enum class Alpha { Opaque, Straight, Additive };
constexpr double Pi = 3.14159265358979323846;
constexpr size_t MaxPixels = 16777216;

struct Options {
    Effect effect = Effect::Mosaic;
    Alpha alpha = Alpha::Opaque;
    uint64_t time = 2;
    int maxsize = 30;
    int maxh = 50;
    double maxomega = 0.2;
    uint32_t bgcolor1 = 0, bgcolor2 = 0;
    int wavetype = 0;
    int centerx = 0, centery = 0;
    int rwidth = 128;
    double roundness = 1.0, speed = 6.0;
    int maxdrift = 24;
    uint32_t bgcolor = 0;
    double twist = 1.0;
};

inline int clamp(int value, int low, int high) {
    return std::max(low, std::min(value, high));
}

inline bool finiteRange(double value, double low, double high) {
    return std::isfinite(value) && value >= low && value <= high;
}

inline void validate(const Options &o, int width, int height) {
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        size_t(width) * size_t(height) > MaxPixels)
        throw std::invalid_argument("extrans image dimensions exceed the pixel limit");
    if (o.time < 2) throw std::invalid_argument("extrans time must be at least 2 ms");
    switch (o.effect) {
    case Effect::Mosaic:
        if (o.maxsize < 2 || o.maxsize > 32768)
            throw std::invalid_argument("mosaic maxsize must be between 2 and 32768");
        break;
    case Effect::Wave:
        if (o.maxh < -32768 || o.maxh > 32768 || !finiteRange(o.maxomega, -1000, 1000) ||
            o.wavetype < 0 || o.wavetype > 2)
            throw std::invalid_argument("invalid wave maxh, maxomega or wavetype");
        break;
    case Effect::Ripple:
        if (o.centerx < 0 || o.centerx >= width || o.centery < 0 || o.centery >= height)
            throw std::invalid_argument("ripple center must be inside the image");
        if (o.rwidth != 16 && o.rwidth != 32 && o.rwidth != 64 && o.rwidth != 128)
            throw std::invalid_argument("ripple rwidth must be 16, 32, 64 or 128");
        if (!finiteRange(o.roundness, 0.000001, 1000000) ||
            !finiteRange(o.speed, -1000000, 1000000))
            throw std::invalid_argument("invalid ripple roundness or speed");
        if (o.maxdrift < 0 || o.maxdrift >= 128 || o.maxdrift >= width || o.maxdrift >= height)
            throw std::invalid_argument("ripple maxdrift must be 0..127 and smaller than both image dimensions");
        break;
    case Effect::RotateSwap:
        if (!finiteRange(o.twist, -1000000, 1000000))
            throw std::invalid_argument("invalid rotateswap twist");
        break;
    }
}

// Channel order is unchanged. The engine converts script background colors to
// the texture's byte order before constructing Options.
inline uint32_t blend(uint32_t a, uint32_t b, unsigned ratio, Alpha alpha) {
    if (!ratio) return a;
    if (ratio >= 255) return b;
    const unsigned inverse = 255 - ratio;
    uint32_t result = 0;
    if (alpha == Alpha::Straight) {
        const unsigned aw = (a >> 24) * inverse, bw = (b >> 24) * ratio;
        const unsigned weight = aw + bw;
        result = ((weight + 127) / 255) << 24;
        if (!weight) return 0;
        for (unsigned shift = 0; shift < 24; shift += 8)
            result |= ((((a >> shift) & 255) * aw + ((b >> shift) & 255) * bw + weight / 2) / weight) << shift;
    } else {
        for (unsigned shift = 0; shift < 32; shift += 8)
            result |= ((((a >> shift) & 255) * inverse + ((b >> shift) & 255) * ratio + 127) / 255) << shift;
        if (alpha == Alpha::Opaque) result |= 0xff000000u;
    }
    return result;
}

struct Image {
    const uint8_t *pixels;
    ptrdiff_t pitch;
    Image(const void *data, ptrdiff_t stride) : pixels(static_cast<const uint8_t *>(data)), pitch(stride) {}
    uint32_t get(int x, int y) const {
        uint32_t value;
        std::memcpy(&value, pixels + ptrdiff_t(y) * pitch + ptrdiff_t(x) * 4, 4);
        return value;
    }
};

class Transition {
    Options options;
    int width, height;
    uint64_t elapsed = 0;
    double progress = 0;
    unsigned ratio = 0;
    int block = 2, offsetx = 0, offsety = 0;
    int waveHeight = 0;
    double omega = 0;
    uint32_t background = 0;
    std::vector<uint16_t> rippleMap;
    std::array<std::array<uint8_t, 2>, 128 * 32> drift;

    struct Transform {
        double cx = 0, cy = 0, scale = 0, sine = 0, cosine = 0;
        bool sample(int x, int y, int width, int height, int &sx, int &sy) const {
            if (scale < 0.000001) return false;
            const double dx = x - cx, dy = (y - cy) / scale;
            const double xx = (dx * cosine - dy * sine) / scale + width / 2;
            const double yy = (dx * sine + dy * cosine) / scale + height / 2;
            // Check in floating point before conversion, including degenerate
            // transforms near the endpoints of a very long transition.
            if (xx < -0.5 || yy < -0.5 || xx >= width - 0.5 || yy >= height - 0.5) return false;
            sx = int(std::floor(xx + 0.5)); sy = int(std::floor(yy + 0.5));
            return true;
        }
    } outgoing, incoming;

    void prepareRipple() {
        rippleMap.resize(size_t(width) * height);
        for (int y = 0; y < height; ++y) {
            const double yy = (std::abs(y - options.centery + 0.5)) * options.roundness;
            for (int x = 0; x < width; ++x) {
                const double xx = std::abs(x - options.centerx + 0.5);
                const int direction = clamp(int(std::atan2(xx, yy) * (64.0 / Pi)), 0, 31);
                const int distance = int(std::fmod(std::floor(std::hypot(xx, yy)), options.rwidth));
                rippleMap[size_t(y) * width + x] = uint16_t(distance * 32 + direction);
            }
        }
    }

    void updateRipple() {
        double phaseValue = std::fmod(options.speed * (double(elapsed) / 1000.0) *
                                     options.rwidth / (2 * Pi), options.rwidth);
        const int phase = options.rwidth - std::max(0, int(phaseValue)) - 1;
        const int amplitude = std::max(0, std::min(int(std::sin(Pi * progress) * options.maxdrift * 4),
                                                 options.maxdrift * 4 - 1));
        for (int distance = 0; distance < options.rwidth; ++distance) {
            const double angle = ((distance + phase) % options.rwidth) * (-2 * Pi / options.rwidth);
            const double value = (std::sin(angle) + std::sin(angle * 2 - 2) * 0.2) / 1.19;
            const int form = int(std::min(1.0, value * value) * 2048 + 0.5);
            const int fd = form * amplitude / 4;
            for (int direction = 0; direction < 32; ++direction) {
                const double rad = Pi / 2 - (direction + 0.5) * (Pi / 64);
                const int cosine = int(std::cos(rad) * 2048 + 0.5);
                const int sine = int(std::sin(rad) * 2048 + 0.5);
                auto &entry = drift[distance * 32 + direction];
                entry[0] = uint8_t((int64_t(cosine) * fd) / (2048 * 2048));
                entry[1] = uint8_t((int64_t(sine) * fd) / (2048 * 2048));
            }
        }
    }

    static int reflect(int value, int size) {
        if (value < 0) value = -value;
        if (value >= size) value = 2 * size - value - 1;
        return clamp(value, 0, size - 1);
    }

    uint32_t pixel(const Image &a, const Image &b, int x, int y) const {
        if (!elapsed) return a.get(x, y);
        if (elapsed == options.time) return b.get(x, y);
        switch (options.effect) {
        case Effect::Mosaic: {
            const int sx = clamp(((x - offsetx) / block) * block + offsetx + block / 2, 0, width - 1);
            const int sy = clamp(((y - offsety) / block) * block + offsety + block / 2, 0, height - 1);
            return blend(a.get(sx, sy), b.get(sx, sy), ratio, options.alpha);
        }
        case Effect::Wave: {
            const int shift = int(std::sin((y - height / 2) * omega) * waveHeight);
            const int sx = x - shift;
            if (sx < 0 || sx >= width) return background;
            return blend(a.get(sx, y), b.get(sx, y), ratio, options.alpha);
        }
        case Effect::Ripple: {
            const auto &delta = drift[rippleMap[size_t(y) * width + x]];
            const int sx = reflect(x + (x < options.centerx ? int(delta[0]) : -int(delta[0])), width);
            const int sy = reflect(y + (y < options.centery ? int(delta[1]) : -int(delta[1])), height);
            return blend(a.get(sx, sy), b.get(sx, sy), ratio, options.alpha);
        }
        case Effect::RotateSwap: {
            int ax, ay, bx, by;
            const bool hasA = outgoing.sample(x, y, width, height, ax, ay);
            const bool hasB = incoming.sample(x, y, width, height, bx, by);
            if (progress < 0.5) {
                if (hasA) return a.get(ax, ay);
                if (hasB) return b.get(bx, by);
            } else {
                if (hasB) return b.get(bx, by);
                if (hasA) return a.get(ax, ay);
            }
            return options.alpha == Alpha::Opaque ? options.bgcolor | 0xff000000u : options.bgcolor;
        }
        }
        return 0;
    }

public:
    Transition(const Options &values, int w, int h) : options(values), width(w), height(h) {
        validate(options, width, height);
        if (options.effect == Effect::Ripple) prepareRipple();
        setTime(0);
    }

    void setTime(uint64_t time) {
        elapsed = std::min(time, options.time);
        progress = double(elapsed) / double(options.time);
        ratio = unsigned(progress * 255);
        switch (options.effect) {
        case Effect::Mosaic: {
            const double half = double(options.time / 2);
            const double triangle = double(std::min(elapsed, options.time - elapsed)) / half;
            block = std::max(2, int((options.maxsize - 2) * triangle) + 2);
            offsetx = (width - block) / 2 - (width / 2 / block) * block;
            offsety = (height - block) / 2 - (height / 2 / block) * block;
            if (offsetx > 0) offsetx -= block;
            if (offsety > 0) offsety -= block;
            break;
        }
        case Effect::Wave: {
            const double envelope = std::sin(Pi * std::min(elapsed, options.time - elapsed) / double(options.time));
            waveHeight = int(envelope * options.maxh);
            if (options.wavetype == 0) omega = envelope * options.maxomega;
            else if (options.wavetype == 1) omega = options.maxomega * progress;
            else omega = options.maxomega * (1 - progress);
            background = blend(options.bgcolor1, options.bgcolor2, ratio, options.alpha);
            break;
        }
        case Effect::Ripple:
            updateRipple();
            break;
        case Effect::RotateSwap: {
            const double a = progress * progress;
            const double b = 1 - (1 - progress) * (1 - progress);
            const int cx = width / 2, cy = height / 2;
            outgoing.cx = int(cx * (1 - a) + std::sin(a * Pi) * cx * 1.5);
            outgoing.cy = int(cy * (1 - a));
            incoming.cx = int((cx - (width - 1)) * b + width - 1 - std::sin(b * Pi) * cx * 1.5);
            incoming.cy = int((cy - (height - 1)) * b + height - 1);
            outgoing.scale = 1 - a; incoming.scale = b;
            outgoing.sine = std::sin(a * options.twist * 2 * Pi);
            outgoing.cosine = std::cos(a * options.twist * 2 * Pi);
            incoming.sine = std::sin((b - 1) * options.twist * 2 * Pi);
            incoming.cosine = std::cos((b - 1) * options.twist * 2 * Pi);
            break;
        }
        }
    }

    void render(const Image &a, const Image &b, uint32_t *dest, int left, int top, int w, int h) const {
        if (!a.pixels || !b.pixels || !dest || left < 0 || top < 0 || w < 0 || h < 0 ||
            left > width || top > height || w > width - left || h > height - top ||
            (a.pitch < ptrdiff_t(width) * 4 && a.pitch > -ptrdiff_t(width) * 4) ||
            (b.pitch < ptrdiff_t(width) * 4 && b.pitch > -ptrdiff_t(width) * 4))
            throw std::invalid_argument("invalid extrans image region or stride");
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                dest[size_t(y) * w + x] = pixel(a, b, left + x, top + y);
    }
};

} // namespace ExtraTransitions
