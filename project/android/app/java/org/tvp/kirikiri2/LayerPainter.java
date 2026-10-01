package org.tvp.kirikiri2;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import java.io.ByteArrayOutputStream;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/** Rasterizes a clipped patch; the engine composites it through its renderer. */
public final class LayerPainter {
    private LayerPainter() {}
    private static final Map<String, Typeface> FONTS = new HashMap<>();
    private static Paint font(String family, float size, int style) {
        if (!(size > 0) || (Float.isNaN(size) || Float.isInfinite(size))) throw new IllegalArgumentException("Invalid font size");
        Typeface face = FONTS.get(family.toLowerCase(Locale.ROOT));
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
        paint.setTypeface(face == null ? Typeface.create(family, style & 3) : Typeface.create(face, style & 3));
        paint.setTextSize(size);
        paint.setUnderlineText((style & 4) != 0);
        paint.setStrikeThruText((style & 8) != 0);
        return paint;
    }
    public static float[] metrics(String family, float size, int style, String text) {
        Paint paint = font(family, size, style);
        Paint.FontMetrics metrics = paint.getFontMetrics();
        String[] lines = text.split("\n", -1);
        float width = 0;
        for (String line : lines) width = Math.max(width, paint.measureText(line));
        float spacing = metrics.descent - metrics.ascent + metrics.leading;
        return new float[] {-metrics.ascent, metrics.descent, -metrics.top, metrics.bottom, spacing, width, spacing * lines.length};
    }
    public static void addFont(String file, String names) {
        Typeface face = Typeface.createFromFile(file);
        for (String name : names.split("\n")) FONTS.put(name.toLowerCase(Locale.ROOT), face);
    }
    private static Matrix matrix(float[] values) {
        if (values.length != 6) throw new IllegalArgumentException("Invalid matrix");
        for (float value : values) if ((Float.isNaN(value) || Float.isInfinite(value))) throw new IllegalArgumentException("Invalid matrix");
        Matrix result = new Matrix();
        result.setValues(new float[] {values[0], values[2], values[4], values[1], values[3], values[5], 0, 0, 1});
        return result;
    }
    private static Path path(float[] commands) {
        Path result = new Path();
        for (int i = 0; i < commands.length;) {
            int op = (int) commands[i++];
            switch (op) {
                case 0: result.moveTo(commands[i], commands[i+1]); i += 2; break;
                case 1: result.lineTo(commands[i], commands[i+1]); i += 2; break;
                case 2: result.cubicTo(commands[i], commands[i+1], commands[i+2], commands[i+3], commands[i+4], commands[i+5]); i += 6; break;
                case 3: result.close(); break;
                case 4: result.addOval(new RectF(commands[i], commands[i+1], commands[i]+commands[i+2], commands[i+1]+commands[i+3]), Path.Direction.CW); i += 4; break;
                case 5: result.arcTo(new RectF(commands[i], commands[i+1], commands[i]+commands[i+2], commands[i+1]+commands[i+3]), commands[i+4], commands[i+5]); i += 6; break;
                default: throw new IllegalArgumentException("Unknown path operation");
            }
        }
        return result;
    }
    private static Paint appearance(int index, int[] colors, float[] styles, boolean antialias) {
        int offset = index * 12;
        Paint paint = new Paint(antialias ? Paint.ANTI_ALIAS_FLAG : 0);
        paint.setColor(colors[index * 2]);
        if (styles[offset] >= 0) {
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(styles[offset] == 0 ? 1 : styles[offset]);
            paint.setStrokeJoin(styles[offset+8] == 1 ? Paint.Join.BEVEL : styles[offset+8] == 2 ? Paint.Join.ROUND : Paint.Join.MITER);
            paint.setStrokeCap(styles[offset+9] == 1 ? Paint.Cap.SQUARE : styles[offset+9] == 2 ? Paint.Cap.ROUND : Paint.Cap.BUTT);
            paint.setStrokeMiter(Math.max(1, styles[offset+10]));
        }
        if (styles[offset+3] == 4) {
            float x0=styles[offset+4], y0=styles[offset+5], x1=styles[offset+6], y1=styles[offset+7];
            if (x0 == x1 && y0 == y1) x1 += 1;
            paint.setShader(new LinearGradient(x0, y0, x1, y1, colors[index*2], colors[index*2+1],
                styles[offset+11] == 0 ? Shader.TileMode.REPEAT : Shader.TileMode.CLAMP));
        }
        return paint;
    }

    // colors: two ARGB values/appearance; styles: width, offsets, brush type,
    // gradient endpoints, join, cap, miter, wrap. A negative width means fill.
    public static int[] render(float[] commands, float[] transform, int[] colors, float[] styles,
            float[] clip, String family, float size, int fontStyle, String text, boolean antialias,
            int[] image, int imageWidth, int imageHeight) {
        if (clip.length != 4 || styles.length % 12 != 0 || colors.length != styles.length / 6)
            throw new IllegalArgumentException("Invalid drawing request");
        Matrix matrix = matrix(transform);
        Path shape;
        boolean bitmapOperation = image != null;
        if (bitmapOperation) {
            shape = new Path();
            shape.addRect(commands[0], commands[1], commands[0]+commands[2], commands[1]+commands[3], Path.Direction.CW);
        } else if (text != null) {
            shape = new Path();
            Paint paint = font(family, size, fontStyle);
            Paint.FontMetrics fm = paint.getFontMetrics();
            float x = commands[0], y = commands[1] - fm.ascent;
            for (String line : text.split("\n", -1)) {
                Path glyphs = new Path();
                paint.getTextPath(line, 0, line.length(), x, y, glyphs);
                shape.addPath(glyphs);
                float width = paint.measureText(line);
                float thickness = Math.max(1, size / 16);
                if ((fontStyle & 4) != 0) shape.addRect(x, y + thickness, x + width, y + thickness * 2, Path.Direction.CW);
                if ((fontStyle & 8) != 0) shape.addRect(x, y + fm.ascent / 3, x + width, y + fm.ascent / 3 + thickness, Path.Direction.CW);
                y += fm.descent - fm.ascent + fm.leading;
            }
        } else shape = path(commands);
        RectF bounds = new RectF();
        boolean first = true;
        int count = bitmapOperation ? 1 : styles.length / 12;
        for (int i = 0; i < count; ++i) {
            Path coverage = new Path();
            if (bitmapOperation) coverage.set(shape);
            else {
                appearance(i, colors, styles, antialias).getFillPath(shape, coverage);
                coverage.offset(styles[i*12+1], styles[i*12+2]);
            }
            coverage.transform(matrix);
            RectF part = new RectF(); coverage.computeBounds(part, true);
            if (first) { bounds.set(part); first = false; } else bounds.union(part);
        }
        if (first || bounds.isEmpty()) return new int[4];
        bounds.inset(-2, -2); // antialias fringe
        if (!bounds.intersect(clip[0], clip[1], clip[2], clip[3])) return new int[4];
        int left = (int)Math.floor(bounds.left), top = (int)Math.floor(bounds.top);
        int width = (int)Math.ceil(bounds.right) - left, height = (int)Math.ceil(bounds.bottom) - top;
        if (width <= 0 || height <= 0 || (long)width * height > 16777216)
            throw new IllegalArgumentException("Drawing patch exceeds bitmap limit");
        Bitmap bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
        try {
            Canvas canvas = new Canvas(bitmap);
            canvas.translate(-left, -top);
            canvas.clipRect(clip[0], clip[1], clip[2], clip[3]);
            canvas.concat(matrix);
            if (bitmapOperation) {
                Bitmap source = Bitmap.createBitmap(image, imageWidth, imageHeight, Bitmap.Config.ARGB_8888);
                try {
                    Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
                    android.graphics.Rect src = new android.graphics.Rect((int)commands[4], (int)commands[5],
                        (int)(commands[4]+commands[6]), (int)(commands[5]+commands[7]));
                    canvas.drawBitmap(source, src, new RectF(commands[0], commands[1], commands[0]+commands[2], commands[1]+commands[3]), paint);
                } finally { source.recycle(); }
            } else {
                for (int i = 0; i < count; ++i) {
                    canvas.save();
                    canvas.translate(styles[i*12+1], styles[i*12+2]);
                    canvas.drawPath(shape, appearance(i, colors, styles, antialias));
                    canvas.restore();
                }
            }
            int[] result = new int[4 + width * height];
            result[0]=left; result[1]=top; result[2]=width; result[3]=height;
            bitmap.getPixels(result, 4, width, 0, 0, width, height);
            return result;
        } finally { bitmap.recycle(); }
    }
    public static byte[] encode(int[] pixels, int width, int height, String mime, int quality) {
        Bitmap.CompressFormat format;
        if (mime.equals("png") || mime.equals("image/png")) format = Bitmap.CompressFormat.PNG;
        else if (mime.equals("jpg") || mime.equals("jpeg") || mime.equals("image/jpeg")) format = Bitmap.CompressFormat.JPEG;
        else if (mime.equals("webp") || mime.equals("image/webp")) format = Bitmap.CompressFormat.WEBP;
        else throw new IllegalArgumentException("Unsupported image encoder: " + mime);
        Bitmap bitmap = Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888);
        try {
            ByteArrayOutputStream output = new ByteArrayOutputStream();
            if (!bitmap.compress(format, Math.max(0, Math.min(100, quality)), output))
                throw new IllegalStateException("Image encoding failed");
            return output.toByteArray();
        } finally { bitmap.recycle(); }
    }
}
