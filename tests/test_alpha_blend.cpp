#include <gtest/gtest.h>
#include "tjsTypes.h"
#include "tvpgl.h"
extern "C" {
extern unsigned char TVPOpacityOnOpacityTable[256 * 256];
extern unsigned char TVPNegativeMulTable[256 * 256];
}
#include "gl/blend_functor_c.h"

TEST(AlphaBlend, OpaqueSourceKeepsEveryColorChannel) {
    alpha_blend_func blend;
    for (tjs_uint32 channel = 0; channel < 256; ++channel) {
        tjs_uint32 source = (channel << 16) | ((255 - channel) << 8) | channel;
        for (tjs_uint32 destination : {0u, 0xffffffffu, 0x713c8ba5u})
            EXPECT_EQ(source, blend(destination, source | 0xff000000, 255));
    }
    EXPECT_EQ(0x336699u, blend(0, 0xff336699, 255));
}

TEST(AlphaBlend, ZeroOpacityAndRepeatedDrawingPreservePixels) {
    alpha_blend_func blend;
    for (tjs_uint32 color : {0u, 0xffffffu, 0x336699u, 0xa0d127u}) {
        EXPECT_EQ(color, blend(color | 0x71000000, 0xff987654, 0));
        tjs_uint32 drawn = 0;
        for (unsigned i = 0; i < 100; ++i)
            drawn = blend(drawn, color, 255);
        EXPECT_EQ(color, drawn);
        for (unsigned opacity = 0; opacity <= 255; ++opacity)
            EXPECT_EQ(color, blend(color, color, opacity));
    }
}

TEST(AlphaBlend, HoldDestinationAlphaKeepsMaskAndOpaqueColor) {
    alpha_blend_HDA_functor blend;
    for (tjs_uint32 alpha = 0; alpha < 256; ++alpha)
        EXPECT_EQ((alpha << 24) | 0x336699u, blend((alpha << 24) | 0xaabbccu, 0xff336699));
}
