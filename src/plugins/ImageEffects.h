#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

// Pixels use straight ARGB. The layer adapter handles the renderer's byte order.
namespace ImageEffects {
inline int clamp(int value, int low, int high) { return std::max(low, std::min(high, value)); }
inline double unit(double value) { return std::max(0.0, std::min(1.0, value)); }
inline uint32_t rgb(uint32_t original, int r, int g, int b) {
    return (original & 0xff000000u) | (uint32_t(clamp(r, 0, 255)) << 16) |
           (uint32_t(clamp(g, 0, 255)) << 8) | uint32_t(clamp(b, 0, 255));
}
inline void light(std::vector<uint32_t> &pixels, int brightness, int contrast) {
    double scale = (100 + clamp(contrast, -100, 100)) / 100.0;
    brightness = clamp(brightness, -255, 255);
    int table[256];
    for(int i = 0; i < 256; ++i) table[i] = clamp(int((i - 128) * scale + brightness + 128), 0, 255);
    for(auto &p : pixels) p = rgb(p, table[(p >> 16) & 255], table[(p >> 8) & 255], table[p & 255]);
}
struct HSL { double h, s, l; };
inline HSL hsl(uint32_t pixel) {
    double r = ((pixel >> 16) & 255) / 255.0, g = ((pixel >> 8) & 255) / 255.0, b = (pixel & 255) / 255.0;
    double hi = std::max(r, std::max(g, b)), lo = std::min(r, std::min(g, b));
    HSL value = {0, 0, (hi + lo) / 2};
    if(hi == lo) return value;
    double delta = hi - lo;
    value.s = delta / (1 - std::abs(2 * value.l - 1));
    value.h = (hi == r ? (g - b) / delta : hi == g ? (b - r) / delta + 2 : (r - g) / delta + 4) / 6;
    value.h -= std::floor(value.h);
    return value;
}
inline uint32_t fromHsl(uint32_t original, HSL value) {
    value.h -= std::floor(value.h);
    double c = (1 - std::abs(2 * value.l - 1)) * value.s;
    double h = value.h * 6, x = c * (1 - std::abs(std::fmod(h, 2) - 1)), m = value.l - c / 2;
    double r = 0, g = 0, b = 0;
    switch(int(h)) {
        case 0: r = c; g = x; break;
        case 1: r = x; g = c; break;
        case 2: g = c; b = x; break;
        case 3: g = x; b = c; break;
        case 4: r = x; b = c; break;
        default: r = c; b = x; break;
    }
    return rgb(original, int(std::lround((r+m)*255)), int(std::lround((g+m)*255)), int(std::lround((b+m)*255)));
}
inline void colorize(std::vector<uint32_t> &pixels, int hue, int saturation, double blend) {
    if(!std::isfinite(blend)) throw std::invalid_argument("Non-finite color blend");
    blend = unit(blend);
    for(auto &p : pixels) {
        HSL value = hsl(p);
        value.h = clamp(hue, 0, 255) / 255.0;
        value.s = clamp(saturation, 0, 255) / 255.0;
        uint32_t tinted = fromHsl(p, value), output = p & 0xff000000u;
        for(int shift : {0, 8, 16})
            output |= uint32_t(std::lround(((p >> shift) & 255) * (1-blend) + ((tinted >> shift) & 255) * blend)) << shift;
        p = output;
    }
}
inline void modulate(std::vector<uint32_t> &pixels, int hue, int saturation, int luminance) {
    double s = clamp(saturation, -100, 100) / 100.0, l = clamp(luminance, -100, 100) / 100.0;
    for(auto &p : pixels) {
        HSL value = hsl(p);
        value.h += (hue % 360) / 360.0;
        value.s += s * (s > 0 ? 1-value.s : value.s);
        value.l += l * (l > 0 ? 1-value.l : value.l);
        p = fromHsl(p, value);
    }
}
inline uint32_t random(uint32_t &state) {
    if(!state) state = 0x85ebca6b;
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}
inline void noise(std::vector<uint32_t> &pixels, int level, uint32_t &state, bool white = false) {
    level = clamp(level, 0, 255);
    for(auto &p : pixels) {
        int value = int(random(state) & 255);
        if(white) p = rgb(p, value, value, value);
        else {
            int offset = (value - 128) * level / 255;
            p = rgb(p, int((p >> 16) & 255)+offset, int((p >> 8) & 255)+offset, int(p & 255)+offset);
        }
    }
}
inline void gaussianBlur(std::vector<uint32_t> &pixels, int width, int height, double radius) {
    if(width < 0 || height < 0 || size_t(width) * size_t(height) != pixels.size() ||
       !std::isfinite(radius) || radius < 0 || radius > 256)
        throw std::invalid_argument("Invalid Gaussian blur dimensions or radius (0..256)");
    if(pixels.empty() || radius == 0) return;
    int extent = std::max(1, int(std::ceil(radius)));
    double sigma = std::max(radius / 3, 0.1), total = 0;
    std::vector<double> kernel(size_t(extent) * 2 + 1);
    for(int i = -extent; i <= extent; ++i) total += kernel[i+extent] = std::exp(-(double(i)*i)/(2*sigma*sigma));
    for(auto &weight : kernel) weight /= total;
    std::vector<uint32_t> buffer(pixels.size());
    for(int pass = 0; pass < 2; ++pass) {
        for(int y = 0; y < height; ++y) for(int x = 0; x < width; ++x) {
            double channels[3] = {};
            for(int d = -extent; d <= extent; ++d) {
                int sx = pass == 0 ? clamp(x+d, 0, width-1) : x;
                int sy = pass == 1 ? clamp(y+d, 0, height-1) : y;
                uint32_t source = pixels[size_t(sy)*width+sx];
                for(int c = 0; c < 3; ++c) channels[c] += ((source >> (c*8)) & 255) * kernel[d+extent];
            }
            size_t at = size_t(y)*width+x;
            buffer[at] = rgb(pixels[at], int(std::lround(channels[2])), int(std::lround(channels[1])), int(std::lround(channels[0])));
        }
        pixels.swap(buffer);
    }
}
}
