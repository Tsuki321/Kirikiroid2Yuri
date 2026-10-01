#pragma once
#include "tjsCommHead.h"
#include "RectItf.h"
#include <array>
#include <vector>
class tTJSNI_Layer;
struct TVPLayerPaintRequest {
    std::vector<float> commands, styles;
    std::vector<tjs_uint32> colors;
    std::array<float, 6> matrix{{1, 0, 0, 1, 0, 0}};
    ttstr family, text;
    float fontSize = 12;
    int fontStyle = 0;
    bool drawText = false, antialias = true;
    const std::vector<tjs_uint32> *image = nullptr;
    int imageWidth = 0, imageHeight = 0;
};
tTVPRect TVPPaintLayer(tTJSNI_Layer *layer, const TVPLayerPaintRequest &request);
std::array<float, 7> TVPMeasureAndroidText(const ttstr &family, float size, int style, const ttstr &text);
std::vector<tjs_uint8> TVPEncodeAndroidImage(const std::vector<tjs_uint32> &pixels, int width, int height, const ttstr &mime, int quality);
void TVPAddAndroidFont(const ttstr &file, const ttstr &names);
