#include <gtest/gtest.h>
#include "ImageEffects.h"
#include <limits>

TEST(ImageEffects, BrightnessContrastAndAlpha) {
    std::vector<uint32_t> pixels = {0x123040f0, 0xff000010};
    ImageEffects::light(pixels, 32, 0);
    EXPECT_EQ(pixels[0], 0x125060ffu);
    EXPECT_EQ(pixels[1], 0xff202030u);
    ImageEffects::light(pixels, 0, -100);
    EXPECT_EQ(pixels[0], 0x12808080u);
}
TEST(ImageEffects, HueRotationAndDesaturation) {
    std::vector<uint32_t> pixels = {0x80ff0000, 0xffffffff};
    ImageEffects::modulate(pixels, 120, 0, 0);
    EXPECT_EQ(pixels[0], 0x8000ff00u);
    EXPECT_EQ(pixels[1], 0xffffffffu);
    ImageEffects::modulate(pixels, 0, -100, 0);
    EXPECT_EQ(pixels[0], 0x80808080u);
}
TEST(ImageEffects, ColorizeEndpoints) {
    std::vector<uint32_t> pixels = {0x7a808080};
    ImageEffects::colorize(pixels, 0, 255, 0);
    EXPECT_EQ(pixels[0], 0x7a808080u);
    ImageEffects::colorize(pixels, 0, 255, 1);
    EXPECT_EQ(pixels[0], 0x7aff0101u);
    EXPECT_THROW(ImageEffects::colorize(pixels, 0, 0, std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
}
TEST(ImageEffects, BlurSpreadsAnImpulseWithoutChangingAlpha) {
    std::vector<uint32_t> pixels(25, 0x32000000);
    pixels[12] = 0xaaffffff;
    ImageEffects::gaussianBlur(pixels, 5, 5, 3);
    EXPECT_EQ(pixels[12] >> 24, 0xaau);
    EXPECT_EQ(pixels[11] >> 24, 0x32u);
    EXPECT_GT(pixels[11] & 255, 0u);
    EXPECT_LT(pixels[12] & 255, 255u);
    EXPECT_EQ(pixels[11], pixels[13]);
    EXPECT_EQ(pixels[7], pixels[17]);
    EXPECT_THROW(ImageEffects::gaussianBlur(pixels, 4, 5, 2), std::invalid_argument);
    EXPECT_THROW(ImageEffects::gaussianBlur(pixels, 5, 5, -1), std::invalid_argument);
}
TEST(ImageEffects, ConstantImagesAndNoisePreserveAlpha) {
    std::vector<uint32_t> pixels(12, 0x15336699);
    ImageEffects::gaussianBlur(pixels, 3, 4, 8);
    for(auto p : pixels) EXPECT_EQ(p, 0x15336699u);
    uint32_t state = 1;
    ImageEffects::noise(pixels, 0, state);
    for(auto p : pixels) EXPECT_EQ(p, 0x15336699u);
    ImageEffects::noise(pixels, 255, state, true);
    EXPECT_NE(pixels[0], pixels[1]);
    for(auto p : pixels) { EXPECT_EQ(p >> 24, 0x15u); EXPECT_EQ(p & 255, (p >> 16) & 255); }
}
