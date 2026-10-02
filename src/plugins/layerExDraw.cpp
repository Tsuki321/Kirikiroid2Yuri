#define NCB_MODULE_NAME TJS_W("layerExDraw.dll")
#include "ncbind/ncbind.hpp"
#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include "GraphicsLoaderIntf.h"
#include "StorageImpl.h"
#include "UtilStreams.h"
#include "android/AndroidLayerPainter.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

namespace Draw {
static void Invalid(const tjs_char *message) {
    TVPThrowExceptionMessage(message);
}
static float Real(const tTJSVariant &value) {
    double number = value.AsReal();
    if (!std::isfinite(number) || std::abs(number) > 1e8)
        Invalid(TJS_W("layerExDraw: invalid coordinate"));
    return static_cast<float>(number);
}
static tTJSVariant Property(const tTJSVariant &value, const tjs_char *name, int index = -1) {
    tTJSVariant result;
    if (value.Type() != tvtObject || !value.AsObjectNoAddRef())
        return result;
    iTJSDispatch2 *object = value.AsObjectNoAddRef();
    if (TJS_FAILED(object->PropGet(0, name, nullptr, &result, object)) && index >= 0)
        object->PropGetByNum(0, index, &result, object);
    return result;
}
static float Number(const tTJSVariant &value, const tjs_char *name, int index, float fallback = 0) {
    tTJSVariant property = Property(value, name, index);
    return property.Type() == tvtVoid ? fallback : Real(property);
}
static std::vector<tTJSVariant> Array(const tTJSVariant &value) {
    if (value.Type() != tvtObject || !value.AsObjectNoAddRef())
        Invalid(TJS_W("layerExDraw: expected array"));
    iTJSDispatch2 *object = value.AsObjectNoAddRef();
    tTJSVariant length;
    object->PropGet(0, TJS_W("count"), nullptr, &length, object);
    tjs_int count = length.AsInteger();
    if (count < 0 || count > 1048576)
        Invalid(TJS_W("layerExDraw: invalid array length"));
    std::vector<tTJSVariant> result(count);
    for (int i = 0; i < count; ++i)
        if (TJS_FAILED(object->PropGetByNum(0, i, &result[i], object)))
            Invalid(TJS_W("layerExDraw: invalid array element"));
    return result;
}
template <class T> static T *Instance(const tTJSVariant &value) {
    if (value.Type() != tvtObject)
        Invalid(TJS_W("layerExDraw: wrong object type"));
    return ncbInstanceAdaptor<T>::GetNativeInstance(value.AsObjectNoAddRef(), true);
}
template <class T> static void Return(tTJSVariant *result, T *instance) {
    std::unique_ptr<T> owned(instance);
    iTJSDispatch2 *object = ncbInstanceAdaptor<T>::CreateAdaptor(instance, false, true);
    if (!object)
        Invalid(TJS_W("layerExDraw: cannot create result object"));
    owned.release();
    if (result)
        *result = tTJSVariant(object, object);
    object->Release();
}
struct Rect {
    tjs_real x, y, width, height;
    Rect(tjs_real x_ = 0, tjs_real y_ = 0, tjs_real width_ = 0, tjs_real height_ = 0)
        : x(x_), y(y_), width(width_), height(height_) {}
    explicit Rect(const tTVPRect &r) : x(r.left), y(r.top), width(r.get_width()), height(r.get_height()) {}
    static Rect From(const tTJSVariant &value) {
        return Rect(Number(value, TJS_W("x"), 0), Number(value, TJS_W("y"), 1),
                    Number(value, TJS_W("width"), 2), Number(value, TJS_W("height"), 3));
    }
#define RECT_FIELD(name, cap)                                                                                \
    tjs_real get##cap() const { return name; }                                                               \
    void set##cap(tjs_real value) { name = value; }
    RECT_FIELD(x, X)
    RECT_FIELD(y, Y) RECT_FIELD(width, Width) RECT_FIELD(height, Height)
#undef RECT_FIELD
        tjs_real getLeft() const {
        return x;
    }
    tjs_real getTop() const { return y; }
    tjs_real getRight() const { return x + width; }
    tjs_real getBottom() const { return y + height; }
    void offset(tjs_real dx, tjs_real dy) {
        x += dx;
        y += dy;
    }
    void inflate(tjs_real dx, tjs_real dy) {
        x -= dx;
        y -= dy;
        width += 2 * dx;
        height += 2 * dy;
    }
};
struct Point {
    float x, y;
};
static Point PointFrom(const tTJSVariant &value) {
    return {Number(value, TJS_W("x"), 0), Number(value, TJS_W("y"), 1)};
}
static std::vector<Point> Points(const tTJSVariant &value) {
    auto values = Array(value);
    std::vector<Point> result;
    if (!values.empty() && values[0].Type() != tvtObject) {
        if (values.size() % 2)
            Invalid(TJS_W("layerExDraw: points require x/y pairs"));
        for (size_t i = 0; i < values.size(); i += 2)
            result.push_back({Real(values[i]), Real(values[i + 1])});
    } else
        for (const auto &point : values)
            result.push_back(PointFrom(point));
    return result;
}
struct Matrix {
    std::array<float, 6> values{{1, 0, 0, 1, 0, 0}};
    Matrix() = default;
    Matrix(tjs_real a, tjs_real b, tjs_real c, tjs_real d, tjs_real x, tjs_real y)
        : values{{float(a), float(b), float(c), float(d), float(x), float(y)}} {}
    static Matrix From(const tTJSVariant &value) {
        if (value.Type() == tvtObject) {
            Matrix *native = ncbInstanceAdaptor<Matrix>::GetNativeInstance(value.AsObjectNoAddRef());
            if (native)
                return *native;
        }
        Matrix result;
        const tjs_char *names[] = {TJS_W("m11"), TJS_W("m12"), TJS_W("m21"),
                                   TJS_W("m22"), TJS_W("dx"),  TJS_W("dy")};
        for (int i = 0; i < 6; ++i)
            result.values[i] = Number(value, names[i], i, i == 0 || i == 3 ? 1 : 0);
        return result;
    }
    static Matrix Product(const Matrix &a, const Matrix &b) {
        const auto &x = a.values;
        const auto &y = b.values;
        return Matrix(x[0] * y[0] + x[2] * y[1], x[1] * y[0] + x[3] * y[1], x[0] * y[2] + x[2] * y[3],
                      x[1] * y[2] + x[3] * y[3], x[0] * y[4] + x[2] * y[5] + x[4],
                      x[1] * y[4] + x[3] * y[5] + x[5]);
    }
    void reset() { *this = Matrix(); }
    void multiply(tTJSVariant matrix, tjs_int order = 0) { combine(From(matrix), order); }
    void combine(const Matrix &other, int order = 0) {
        *this = order == 1 ? Product(other, *this) : Product(*this, other);
    }
    void translate(tjs_real x, tjs_real y, tjs_int order = 0) { combine(Matrix(1, 0, 0, 1, x, y), order); }
    void scale(tjs_real x, tjs_real y, tjs_int order = 0) { combine(Matrix(x, 0, 0, y, 0, 0), order); }
    void rotate(tjs_real degrees, tjs_int order = 0) {
        double radians = degrees * 3.14159265358979323846 / 180;
        combine(Matrix(std::cos(radians), std::sin(radians), -std::sin(radians), std::cos(radians), 0, 0),
                order);
    }
    void invert() {
        auto v = values;
        double det = v[0] * v[3] - v[1] * v[2];
        if (!std::isfinite(det) || std::abs(det) < 1e-12)
            Invalid(TJS_W("layerExDraw: singular matrix"));
        *this = Matrix(v[3] / det, -v[1] / det, -v[2] / det, v[0] / det, (v[2] * v[5] - v[3] * v[4]) / det,
                       (v[1] * v[4] - v[0] * v[5]) / det);
    }
};
struct Font {
    ttstr family;
    tjs_real size;
    tjs_int style;
    bool selfPath = false;
    Font(ttstr family_, tjs_real size_, tjs_int style_) : family(family_), size(size_), style(style_) {
        if (!(size > 0) || !std::isfinite(size))
            Invalid(TJS_W("layerExDraw: invalid font size"));
    }
    ttstr getFamilyName() const { return family; }
    void setFamilyName(ttstr value) { family = value; }
    tjs_real getEmSize() const { return size; }
    void setEmSize(tjs_real value) {
        if (!(value > 0) || !std::isfinite(value))
            Invalid(TJS_W("Invalid font size"));
        size = value;
    }
    tjs_int getStyle() const { return style; }
    void setStyle(tjs_int value) { style = value; }
    bool getForceSelfPathDraw() const { return selfPath; }
    void setForceSelfPathDraw(bool value) { selfPath = value; }
    tjs_real metric(int i) const { return TVPMeasureAndroidText(family, float(size), style, ttstr())[i]; }
    tjs_real getAscent() const { return metric(0); }
    tjs_real getDescent() const { return metric(1); }
    tjs_real getAscentLeading() const { return metric(2); }
    tjs_real getDescentLeading() const { return metric(3); }
    tjs_real getLineSpacing() const { return metric(4); }
};
struct Paint {
    std::array<tjs_uint32, 2> colors{{0xff000000, 0xff000000}};
    std::array<float, 12> style{{-1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 10, 1}};
};
struct Appearance {
    std::vector<Paint> paints;
    void clear() { paints.clear(); }
    Paint brush(const tTJSVariant &value, float ox, float oy) {
        Paint result;
        result.style[1] = ox;
        result.style[2] = oy;
        if (value.Type() != tvtObject)
            result.colors[0] = result.colors[1] = static_cast<tjs_uint32>(value.AsInteger());
        else {
            int type = int(Number(value, TJS_W("type"), -1));
            result.style[3] = float(type);
            if (type == 0)
                result.colors[0] = result.colors[1] =
                    static_cast<tjs_uint32>(Property(value, TJS_W("color")).AsInteger());
            else if (type == 4) {
                result.colors[0] = static_cast<tjs_uint32>(Property(value, TJS_W("color1")).AsInteger());
                result.colors[1] = static_cast<tjs_uint32>(Property(value, TJS_W("color2")).AsInteger());
                tTJSVariant first = Property(value, TJS_W("point1"));
                Point a, b;
                if (first.Type() != tvtVoid) {
                    a = PointFrom(first);
                    b = PointFrom(Property(value, TJS_W("point2")));
                } else {
                    Rect r = Rect::From(Property(value, TJS_W("rect")));
                    double angle = Number(value, TJS_W("angle"), -1, -99999);
                    if (angle == -99999) {
                        int mode = int(Number(value, TJS_W("mode"), -1));
                        angle = mode == 1 ? 90 : mode == 2 ? 45 : mode == 3 ? 135 : 0;
                    }
                    double radians = angle * 3.14159265358979323846 / 180;
                    double extent =
                        std::abs(r.width * std::cos(radians)) + std::abs(r.height * std::sin(radians));
                    a = {float(r.x + r.width / 2 - std::cos(radians) * extent / 2),
                         float(r.y + r.height / 2 - std::sin(radians) * extent / 2)};
                    b = {float(r.x + r.width / 2 + std::cos(radians) * extent / 2),
                         float(r.y + r.height / 2 + std::sin(radians) * extent / 2)};
                }
                result.style[4] = a.x;
                result.style[5] = a.y;
                result.style[6] = b.x;
                result.style[7] = b.y;
                result.style[11] = Number(value, TJS_W("wrapMode"), -1, 1);
            } else
                Invalid(TJS_W(
                    "layerExDraw: this brush type is not supported; use a solid or linear-gradient brush"));
        }
        return result;
    }
    static tjs_error TJS_INTF_METHOD add(tTJSVariant *, tjs_int count, tTJSVariant **args,
                                         iTJSDispatch2 *self, bool pen) {
        if (count < (pen ? 2 : 1))
            return TJS_E_BADPARAMCOUNT;
        Appearance *appearance = ncbInstanceAdaptor<Appearance>::GetNativeInstance(self, true);
        int index = pen ? 2 : 1;
        Paint paint = appearance->brush(*args[0], count > index ? Real(*args[index]) : 0,
                                        count > index + 1 ? Real(*args[index + 1]) : 0);
        if (pen) {
            paint.style[0] =
                args[1]->Type() == tvtObject ? Number(*args[1], TJS_W("width"), -1, 1) : Real(*args[1]);
            if (paint.style[0] < 0)
                return TJS_E_INVALIDPARAM;
            if (args[1]->Type() == tvtObject) {
                paint.style[8] = Number(*args[1], TJS_W("lineJoin"), -1);
                paint.style[9] = Number(*args[1], TJS_W("startCap"), -1);
                paint.style[10] = Number(*args[1], TJS_W("miterLimit"), -1, 10);
                if (Number(*args[1], TJS_W("dashStyle"), -1) != 0)
                    Invalid(TJS_W("layerExDraw: dashed pens are not yet supported"));
            }
        }
        appearance->paints.push_back(paint);
        return TJS_S_OK;
    }
    static tjs_error TJS_INTF_METHOD addBrush(tTJSVariant *r, tjs_int n, tTJSVariant **p, iTJSDispatch2 *o) {
        return add(r, n, p, o, false);
    }
    static tjs_error TJS_INTF_METHOD addPen(tTJSVariant *r, tjs_int n, tTJSVariant **p, iTJSDispatch2 *o) {
        return add(r, n, p, o, true);
    }
};
enum Operation {
    Line,
    Lines,
    Rectangle,
    Rectangles,
    Ellipse,
    Arc,
    Pie,
    Bezier,
    Beziers,
    Polygon,
    Curve,
    Curve2,
    Curve3,
    ClosedCurve,
    ClosedCurve2,
    DrawPath,
    Text,
    Measure,
    ImageAt,
    ImageRect,
    ImageStretch,
    ImageAffine,
    Clear,
    SetTransform,
    ResetTransform,
    Rotate,
    Scale,
    Translate,
    SetView,
    ResetView,
    RotateView,
    ScaleView,
    TranslateView,
    Save,
    ColorRegions
};
struct Path {
    std::vector<float> commands;
    bool fresh = true;
    void startFigure() { fresh = true; }
    void closeFigure() {
        commands.push_back(3);
        fresh = true;
    }
    void move(Point point) {
        commands.insert(commands.end(), {0, point.x, point.y});
        fresh = false;
    }
    void line(Point point) {
        commands.insert(commands.end(), {1, point.x, point.y});
        fresh = false;
    }
    void begin(Point point) {
        if (fresh)
            move(point);
        else
            line(point);
    }
    void cubic(Point a, Point b, Point c) {
        commands.insert(commands.end(), {2, a.x, a.y, b.x, b.y, c.x, c.y});
        fresh = false;
    }
    void rectangle(const Rect &r) {
        move({float(r.x), float(r.y)});
        line({float(r.x + r.width), float(r.y)});
        line({float(r.x + r.width), float(r.y + r.height)});
        line({float(r.x), float(r.y + r.height)});
        closeFigure();
    }
    void curve(const std::vector<Point> &points, bool closed, float tension, int offset = 0,
               int segments = -1) {
        int length = int(points.size());
        if (segments < 0)
            segments = closed ? length : length - 1;
        if (length < 2 || offset < 0 || segments < 1 || offset + segments > (closed ? length : length - 1))
            Invalid(TJS_W("layerExDraw: invalid curve"));
        auto at = [&](int i) {
            return points[closed ? (i + length) % length : std::max(0, std::min(length - 1, i))];
        };
        begin(at(offset));
        for (int i = offset; i < offset + segments; ++i) {
            Point a = at(i - 1), b = at(i), c = at(i + 1), d = at(i + 2);
            cubic({b.x + (c.x - a.x) * tension / 3, b.y + (c.y - a.y) * tension / 3},
                  {c.x - (d.x - b.x) * tension / 3, c.y - (d.y - b.y) * tension / 3}, c);
        }
        if (closed)
            closeFigure();
    }
    void operation(Operation op, int count, tTJSVariant **args) {
        auto need = [&](int n) {
            if (count < n)
                Invalid(TJS_W("layerExDraw: not enough drawing arguments"));
        };
        auto n = [&](int i) {
            need(i + 1);
            return Real(*args[i]);
        };
        switch (op) {
        case Line:
            need(4);
            begin({n(0), n(1)});
            line({n(2), n(3)});
            break;
        case Rectangle:
            need(4);
            rectangle(Rect(n(0), n(1), n(2), n(3)));
            break;
        case Rectangles:
            need(1);
            for (const auto &r : Array(*args[0]))
                rectangle(Rect::From(r));
            break;
        case Ellipse:
            need(4);
            commands.insert(commands.end(), {4, n(0), n(1), n(2), n(3)});
            fresh = true;
            break;
        case Arc:
        case Pie: {
            need(6);
            float x = n(0), y = n(1), w = n(2), h = n(3), angle = n(4), sweep = n(5);
            if (op == Pie) {
                move({x + w / 2, y + h / 2});
                double r = angle * 3.14159265358979323846 / 180;
                line({x + w / 2 + float(std::cos(r)) * w / 2, y + h / 2 + float(std::sin(r)) * h / 2});
            }
            commands.insert(commands.end(), {5, x, y, w, h, angle, sweep});
            fresh = false;
            if (op == Pie)
                closeFigure();
            break;
        }
        case Bezier:
            need(8);
            begin({n(0), n(1)});
            cubic({n(2), n(3)}, {n(4), n(5)}, {n(6), n(7)});
            break;
        case Lines:
        case Polygon:
        case Beziers:
        case Curve:
        case Curve2:
        case Curve3:
        case ClosedCurve:
        case ClosedCurve2: {
            need(1);
            auto points = Points(*args[0]);
            if (points.size() < 2)
                Invalid(TJS_W("layerExDraw: too few points"));
            if (op == Curve || op == Curve2 || op == Curve3 || op == ClosedCurve || op == ClosedCurve2) {
                float tension = (op == Curve2 || op == ClosedCurve2) ? n(1) : op == Curve3 ? n(3) : 0.5f;
                curve(points, op == ClosedCurve || op == ClosedCurve2, tension, op == Curve3 ? int(n(1)) : 0,
                      op == Curve3 ? int(n(2)) : -1);
            } else {
                begin(points[0]);
                if (op == Beziers) {
                    if ((points.size() - 1) % 3)
                        Invalid(TJS_W("layerExDraw: Bezier points must be 1+3n"));
                    for (size_t i = 1; i < points.size(); i += 3)
                        cubic(points[i], points[i + 1], points[i + 2]);
                } else
                    for (size_t i = 1; i < points.size(); ++i)
                        line(points[i]);
                if (op == Polygon)
                    closeFigure();
            }
            break;
        }
        default:
            Invalid(TJS_W("layerExDraw: invalid path operation"));
        }
    }
    template <int Op>
    static tjs_error TJS_INTF_METHOD invoke(tTJSVariant *, tjs_int n, tTJSVariant **p, iTJSDispatch2 *o) {
        ncbInstanceAdaptor<Path>::GetNativeInstance(o, true)->operation(Operation(Op), n, p);
        return TJS_S_OK;
    }
};
struct Image {
    int width = 0, height = 0;
    std::vector<tjs_uint32> pixels;
    explicit Image(ttstr name) {
        tTVPBaseBitmap bitmap(1, 1, 32);
        TVPLoadGraphic(&bitmap, name, TVP_clNone, 0, 0, glmNormal);
        width = bitmap.GetWidth();
        height = bitmap.GetHeight();
        if (width <= 0 || height <= 0 || uint64_t(width) * height > 16777216)
            Invalid(TJS_W("layerExDraw: image too large"));
        pixels.resize(size_t(width) * height);
        for (int y = 0; y < height; ++y) {
            const tjs_uint32 *row = static_cast<const tjs_uint32 *>(bitmap.GetScanLine(y));
            for (int x = 0; x < width; ++x)
                pixels[size_t(y) * width + x] = TVP_REVRGB(row[x]);
        }
    }
    tjs_int getWidth() const { return width; }
    tjs_int getHeight() const { return height; }
    static tjs_error TJS_INTF_METHOD load(tTJSVariant *r, tjs_int n, tTJSVariant **p, iTJSDispatch2 *) {
        if (n < 1)
            return TJS_E_BADPARAMCOUNT;
        Return(r, new Image(ttstr(*p[0])));
        return TJS_S_OK;
    }
    static tjs_error TJS_INTF_METHOD bounds(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *o) {
        auto image = ncbInstanceAdaptor<Image>::GetNativeInstance(o, true);
        Return(r, new Rect(0, 0, image->width, image->height));
        return TJS_S_OK;
    }
};
struct LayerDraw {
    tTJSNI_Layer *layer = nullptr;
    Matrix transform, view;
    int smoothing = 4, textHint = 4;
    explicit LayerDraw(iTJSDispatch2 *object) {
        if (!object ||
            TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                                                     reinterpret_cast<iTJSNativeInstance **>(&layer))) ||
            !layer)
            Invalid(TJS_W("layerExDraw: expected Layer"));
    }
    tjs_int getSmoothingMode() const { return smoothing; }
    void setSmoothingMode(tjs_int value) { smoothing = value; }
    tjs_int getTextRenderingHint() const { return textHint; }
    void setTextRenderingHint(tjs_int value) { textHint = value; }
    std::vector<tjs_uint32> pixels() {
        auto bitmap = layer->GetMainImage();
        if (!bitmap)
            Invalid(TJS_W("layerExDraw: layer has no image"));
        int width = bitmap->GetWidth(), height = bitmap->GetHeight();
        if (uint64_t(width) * height > 16777216)
            Invalid(TJS_W("layerExDraw: image too large"));
        std::vector<tjs_uint32> result(size_t(width) * height);
        for (int y = 0; y < height; ++y) {
            auto row = static_cast<const tjs_uint32 *>(bitmap->GetScanLine(y));
            for (int x = 0; x < width; ++x)
                result[size_t(y) * width + x] = TVP_REVRGB(row[x]);
        }
        return result;
    }
    void operation(Operation op, tTJSVariant *result, int count, tTJSVariant **args) {
        auto need = [&](int n) {
            if (count < n)
                Invalid(TJS_W("layerExDraw: not enough arguments"));
        };
        auto n = [&](int i) {
            need(i + 1);
            return Real(*args[i]);
        };
        if (result)
            result->Clear();
        if (op >= SetTransform && op <= TranslateView) {
            Matrix &m = op >= SetView ? view : transform;
            int operation = op >= SetView ? op - SetView : op - SetTransform;
            switch (operation) {
            case 0:
                need(1);
                m = Matrix::From(*args[0]);
                break;
            case 1:
                m.reset();
                break;
            case 2:
                m.rotate(n(0), count > 1 ? int(n(1)) : 0);
                break;
            case 3:
                m.scale(n(0), n(1), count > 2 ? int(n(2)) : 0);
                break;
            case 4:
                m.translate(n(0), n(1), count > 2 ? int(n(2)) : 0);
                break;
            }
            return;
        }
        if (op == Clear) {
            tjs_uint32 color = count ? static_cast<tjs_uint32>(args[0]->AsInteger()) : 0;
            layer->GetMainImage()->Fill(tTVPRect(0, 0, layer->GetImageWidth(), layer->GetImageHeight()),
                                        color);
            layer->SetImageModified(true);
            layer->Update();
            return;
        }
        if (op == Save) {
            need(1);
            auto image = pixels();
            ttstr mime = count > 1 ? ttstr(*args[1]) : ttstr(TJS_W("image/png"));
            auto encoded =
                TVPEncodeAndroidImage(image, layer->GetImageWidth(), layer->GetImageHeight(), mime, 95);
            std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(ttstr(*args[0]), TJS_BS_WRITE));
            stream->WriteBuffer(encoded.data(), encoded.size());
            if (result)
                *result = tjs_int(1);
            return;
        }
        if (op == ColorRegions) {
            need(1);
            tjs_uint32 color = static_cast<tjs_uint32>(args[0]->AsInteger());
            auto image = pixels();
            int width = layer->GetImageWidth(), height = layer->GetImageHeight();
            std::vector<Rect> rectangles;
            std::map<std::pair<int, int>, size_t> previous;
            for (int y = 0; y < height; ++y) {
                std::map<std::pair<int, int>, size_t> current;
                for (int x = 0; x < width;) {
                    if (image[size_t(y) * width + x] != color) {
                        ++x;
                        continue;
                    }
                    int first = x;
                    while (x < width && image[size_t(y) * width + x] == color)
                        ++x;
                    auto key = std::make_pair(first, x - first);
                    auto found = previous.find(key);
                    size_t index;
                    if (found != previous.end()) {
                        index = found->second;
                        rectangles[index].height++;
                    } else {
                        index = rectangles.size();
                        if (index >= 100000)
                            Invalid(TJS_W("layerExDraw: too many color regions"));
                        rectangles.emplace_back(first, y, x - first, 1);
                    }
                    current[key] = index;
                }
                previous.swap(current);
            }
            iTJSDispatch2 *array = TJSCreateArrayObject();
            for (size_t i = 0; i < rectangles.size(); ++i) {
                tTJSVariant rect;
                Return(&rect, new Rect(rectangles[i]));
                array->PropSetByNum(TJS_MEMBERENSURE, i, &rect, array);
            }
            if (result)
                *result = tTJSVariant(array, array);
            array->Release();
            return;
        }
        TVPLayerPaintRequest request;
        request.matrix = Matrix::Product(view, transform).values;
        request.antialias = smoothing != 1 && smoothing != 3;
        Appearance *appearance = nullptr;
        if (op == Text || op == Measure) {
            need(op == Text ? 5 : 2);
            Font *font = Instance<Font>(*args[0]);
            request.family = font->family;
            request.fontSize = font->size;
            request.fontStyle = font->style;
            if (op == Measure) {
                auto metric =
                    TVPMeasureAndroidText(font->family, float(font->size), font->style, ttstr(*args[1]));
                Return(result, new Rect(0, 0, metric[5], metric[6]));
                return;
            }
            appearance = Instance<Appearance>(*args[1]);
            request.commands = {n(2), n(3)};
            request.text = *args[4];
            request.drawText = true;
            request.antialias = textHint != 1 && textHint != 2;
        } else if (op >= ImageAt && op <= ImageAffine) {
            Image *image = nullptr;
            float dx = 0, dy = 0, dw = 0, dh = 0, sx = 0, sy = 0, sw = 0, sh = 0;
            if (op == ImageAt) {
                need(3);
                dx = n(0);
                dy = n(1);
                image = Instance<Image>(*args[2]);
                dw = sw = image->width;
                dh = sh = image->height;
            } else if (op == ImageRect) {
                need(7);
                dx = n(0);
                dy = n(1);
                image = Instance<Image>(*args[2]);
                sx = n(3);
                sy = n(4);
                dw = sw = n(5);
                dh = sh = n(6);
            } else if (op == ImageStretch) {
                need(9);
                dx = n(0);
                dy = n(1);
                dw = n(2);
                dh = n(3);
                image = Instance<Image>(*args[4]);
                sx = n(5);
                sy = n(6);
                sw = n(7);
                sh = n(8);
            } else {
                need(12);
                image = Instance<Image>(*args[0]);
                sx = n(1);
                sy = n(2);
                sw = dw = n(3);
                sh = dh = n(4);
                Matrix affine;
                if (args[5]->AsInteger())
                    affine = Matrix(n(6), n(7), n(8), n(9), n(10), n(11));
                else {
                    if (sw <= 0 || sh <= 0)
                        Invalid(TJS_W("layerExDraw: empty source rectangle"));
                    affine = Matrix((n(8) - n(6)) / sw, (n(9) - n(7)) / sw, (n(10) - n(6)) / sh,
                                    (n(11) - n(7)) / sh, n(6), n(7));
                }
                Matrix base;
                base.values = request.matrix;
                request.matrix = Matrix::Product(base, affine).values;
            }
            if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 || sx < 0 || sy < 0 || sx + sw > image->width ||
                sy + sh > image->height)
                Invalid(TJS_W("layerExDraw: invalid image rectangle"));
            request.commands = {dx, dy, dw, dh, sx, sy, sw, sh};
            request.image = &image->pixels;
            request.imageWidth = image->width;
            request.imageHeight = image->height;
        } else {
            need(op == DrawPath ? 2 : 1);
            appearance = Instance<Appearance>(*args[0]);
            if (op == DrawPath)
                request.commands = Instance<Path>(*args[1])->commands;
            else {
                Path path;
                path.operation(op, count - 1, args + 1);
                request.commands.swap(path.commands);
            }
        }
        if (appearance)
            for (const Paint &paint : appearance->paints) {
                request.colors.insert(request.colors.end(), paint.colors.begin(), paint.colors.end());
                request.styles.insert(request.styles.end(), paint.style.begin(), paint.style.end());
            }
        Return(result, new Rect(TVPPaintLayer(layer, request)));
    }
    template <int Op>
    static tjs_error TJS_INTF_METHOD invoke(tTJSVariant *result, tjs_int count, tTJSVariant **args,
                                            iTJSDispatch2 *object) {
        LayerDraw *instance = ncbInstanceAdaptor<LayerDraw>::GetNativeInstance(object);
        if (!instance) {
            std::unique_ptr<LayerDraw> created(new LayerDraw(object));
            ncbInstanceAdaptor<LayerDraw>::SetAdaptorWithNativeInstance(object, created.get(), true);
            instance = created.release();
        }
        instance->operation(Operation(Op), result, count, args);
        return TJS_S_OK;
    }
};
struct GdiPlus {};
} // namespace Draw

using Draw::Appearance;
using Draw::Font;
using Draw::GdiPlus;
using Draw::Image;
using Draw::LayerDraw;
using Draw::Matrix;
using Draw::Path;
using Draw::Rect;

NCB_REGISTER_SUBCLASS(Rect) {
    NCB_CONSTRUCTOR((tjs_real, tjs_real, tjs_real, tjs_real));
    NCB_PROPERTY(x, getX, setX);
    NCB_PROPERTY(y, getY, setY);
    NCB_PROPERTY(width, getWidth, setWidth);
    NCB_PROPERTY(height, getHeight, setHeight);
    NCB_PROPERTY_RO(left, getLeft);
    NCB_PROPERTY_RO(top, getTop);
    NCB_PROPERTY_RO(right, getRight);
    NCB_PROPERTY_RO(bottom, getBottom);
    NCB_METHOD(offset);
    NCB_METHOD(inflate);
}
NCB_REGISTER_SUBCLASS(Matrix) {
    NCB_CONSTRUCTOR((tjs_real, tjs_real, tjs_real, tjs_real, tjs_real, tjs_real));
    NCB_METHOD(reset);
    NCB_METHOD(multiply);
    NCB_METHOD(translate);
    NCB_METHOD(scale);
    NCB_METHOD(rotate);
    NCB_METHOD(invert);
}
NCB_REGISTER_SUBCLASS(Font) {
    NCB_CONSTRUCTOR((ttstr, tjs_real, tjs_int));
    NCB_PROPERTY(familyName, getFamilyName, setFamilyName);
    NCB_PROPERTY(emSize, getEmSize, setEmSize);
    NCB_PROPERTY(style, getStyle, setStyle);
    NCB_PROPERTY(forceSelfPathDraw, getForceSelfPathDraw, setForceSelfPathDraw);
    NCB_PROPERTY_RO(ascent, getAscent);
    NCB_PROPERTY_RO(descent, getDescent);
    NCB_PROPERTY_RO(ascentLeading, getAscentLeading);
    NCB_PROPERTY_RO(descentLeading, getDescentLeading);
    NCB_PROPERTY_RO(lineSpacing, getLineSpacing);
}
NCB_REGISTER_SUBCLASS(Appearance) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD(clear);
    RawCallback(TJS_W("addBrush"), &Appearance::addBrush, 0);
    RawCallback(TJS_W("addPen"), &Appearance::addPen, 0);
}
#define PATH_METHOD(name, op) RawCallback(TJS_W(#name), &Path::invoke<Draw::op>, 0)
NCB_REGISTER_SUBCLASS(Path) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD(startFigure);
    NCB_METHOD(closeFigure);
    PATH_METHOD(drawLine, Line);
    PATH_METHOD(drawLines, Lines);
    PATH_METHOD(drawRectangle, Rectangle);
    PATH_METHOD(drawRectangles, Rectangles);
    PATH_METHOD(drawEllipse, Ellipse);
    PATH_METHOD(drawArc, Arc);
    PATH_METHOD(drawPie, Pie);
    PATH_METHOD(drawBezier, Bezier);
    PATH_METHOD(drawBeziers, Beziers);
    PATH_METHOD(drawPolygon, Polygon);
    PATH_METHOD(drawCurve, Curve);
    PATH_METHOD(drawCurve2, Curve2);
    PATH_METHOD(drawCurve3, Curve3);
    PATH_METHOD(drawClosedCurve, ClosedCurve);
    PATH_METHOD(drawClosedCurve2, ClosedCurve2);
}
#undef PATH_METHOD
NCB_REGISTER_SUBCLASS(Image) {
    NCB_CONSTRUCTOR((ttstr));
    NCB_PROPERTY_RO(width, getWidth);
    NCB_PROPERTY_RO(height, getHeight);
    RawCallback(TJS_W("load"), &Image::load, TJS_STATICMEMBER);
    RawCallback(TJS_W("getBounds"), &Image::bounds, 0);
}
NCB_REGISTER_CLASS(GdiPlus) {
    NCB_SUBCLASS(RectF, Rect);
    NCB_SUBCLASS(Matrix, Matrix);
    NCB_SUBCLASS(Font, Font);
    NCB_SUBCLASS(Appearance, Appearance);
    NCB_SUBCLASS(Path, Path);
    NCB_SUBCLASS(Image, Image);
    RawCallback(TJS_W("loadImage"), &Image::load, TJS_STATICMEMBER);
    Variant(TJS_W("Ok"), 0);
    Variant(TJS_W("FontStyleRegular"), 0);
    Variant(TJS_W("FontStyleBold"), 1);
    Variant(TJS_W("FontStyleItalic"), 2);
    Variant(TJS_W("FontStyleBoldItalic"), 3);
    Variant(TJS_W("FontStyleUnderline"), 4);
    Variant(TJS_W("FontStyleStrikeout"), 8);
    Variant(TJS_W("BrushTypeSolidColor"), 0);
    Variant(TJS_W("BrushTypeLinearGradient"), 4);
    Variant(TJS_W("MatrixOrderPrepend"), 0);
    Variant(TJS_W("MatrixOrderAppend"), 1);
    Variant(TJS_W("SmoothingModeDefault"), 0);
    Variant(TJS_W("SmoothingModeHighSpeed"), 1);
    Variant(TJS_W("SmoothingModeHighQuality"), 2);
    Variant(TJS_W("SmoothingModeNone"), 3);
    Variant(TJS_W("SmoothingModeAntiAlias"), 4);
    Variant(TJS_W("TextRenderingHintSystemDefault"), 0);
    Variant(TJS_W("TextRenderingHintSingleBitPerPixelGridFit"), 1);
    Variant(TJS_W("TextRenderingHintSingleBitPerPixel"), 2);
    Variant(TJS_W("TextRenderingHintAntiAliasGridFit"), 3);
    Variant(TJS_W("TextRenderingHintAntiAlias"), 4);
    Variant(TJS_W("TextRenderingHintClearTypeGridFit"), 5);
    Variant(TJS_W("LinearGradientModeHorizontal"), 0);
    Variant(TJS_W("LinearGradientModeVertical"), 1);
    Variant(TJS_W("LinearGradientModeForwardDiagonal"), 2);
    Variant(TJS_W("LinearGradientModeBackwardDiagonal"), 3);
    Variant(TJS_W("WrapModeTile"), 0);
    Variant(TJS_W("WrapModeClamp"), 4);
    Variant(TJS_W("LineJoinMiter"), 0);
    Variant(TJS_W("LineJoinBevel"), 1);
    Variant(TJS_W("LineJoinRound"), 2);
    Variant(TJS_W("LineCapFlat"), 0);
    Variant(TJS_W("LineCapSquare"), 1);
    Variant(TJS_W("LineCapRound"), 2);
    Variant(TJS_W("DashStyleSolid"), 0);
}
#define LAYER_METHOD(name, op) RawCallback(TJS_W(#name), &LayerDraw::invoke<Draw::op>, 0)
NCB_GET_INSTANCE_HOOK(LayerDraw){NCB_INSTANCE_GETTER(object){ClassT *instance = GetNativeInstance(object);
if (!instance) {
    std::unique_ptr<ClassT> created(new ClassT(object));
    SetNativeInstance(object, created.get());
    instance = created.release();
}
return instance;
}
}
;
NCB_ATTACH_CLASS_WITH_HOOK(LayerDraw, Layer) {
    NCB_PROPERTY(smoothingMode, getSmoothingMode, setSmoothingMode);
    NCB_PROPERTY(textRenderingHint, getTextRenderingHint, setTextRenderingHint);
    LAYER_METHOD(drawString, Text);
    LAYER_METHOD(measureString, Measure);
    LAYER_METHOD(drawLine, Line);
    LAYER_METHOD(drawLines, Lines);
    LAYER_METHOD(drawRectangle, Rectangle);
    LAYER_METHOD(drawRect, Rectangle);
    LAYER_METHOD(drawRectangles, Rectangles);
    LAYER_METHOD(drawEllipse, Ellipse);
    LAYER_METHOD(drawArc, Arc);
    LAYER_METHOD(drawPie, Pie);
    LAYER_METHOD(drawBezier, Bezier);
    LAYER_METHOD(drawBeziers, Beziers);
    LAYER_METHOD(drawPolygon, Polygon);
    LAYER_METHOD(drawCurve, Curve);
    LAYER_METHOD(drawCurve2, Curve2);
    LAYER_METHOD(drawCurve3, Curve3);
    LAYER_METHOD(drawClosedCurve, ClosedCurve);
    LAYER_METHOD(drawClosedCurve2, ClosedCurve2);
    LAYER_METHOD(drawPath, DrawPath);
    LAYER_METHOD(drawImage, ImageAt);
    LAYER_METHOD(drawImageRect, ImageRect);
    LAYER_METHOD(drawImageStretch, ImageStretch);
    LAYER_METHOD(drawImageAffine, ImageAffine);
    LAYER_METHOD(clear, Clear);
    LAYER_METHOD(setTransform, SetTransform);
    LAYER_METHOD(resetTransform, ResetTransform);
    LAYER_METHOD(rotateTransform, Rotate);
    LAYER_METHOD(scaleTransform, Scale);
    LAYER_METHOD(translateTransform, Translate);
    LAYER_METHOD(setViewTransform, SetView);
    LAYER_METHOD(resetViewTransform, ResetView);
    LAYER_METHOD(rotateViewTransform, RotateView);
    LAYER_METHOD(scaleViewTransform, ScaleView);
    LAYER_METHOD(translateViewTransform, TranslateView);
    LAYER_METHOD(saveImage, Save);
    LAYER_METHOD(getColorRegionRects, ColorRegions);
}
#undef LAYER_METHOD
