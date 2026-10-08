package org.tvp.kirikiri2;

import android.app.Activity;
import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Small shared palette for the library and the game controls. */
public final class ModernUi {
    public static final int BACKGROUND = Color.rgb(16, 19, 28);
    public static final int SURFACE = Color.rgb(29, 34, 48);
    public static final int BORDER = Color.rgb(56, 64, 86);
    public static final int TEXT = Color.rgb(244, 244, 252);
    public static final int MUTED = Color.rgb(172, 180, 201);
    public static final int ACCENT = Color.rgb(186, 176, 255);

    private ModernUi() {}

    public static int dp(Context context, float value) {
        return Math.round(value * context.getResources().getDisplayMetrics().density);
    }

    public static GradientDrawable shape(Context context, int color, int radius) {
        GradientDrawable background = new GradientDrawable();
        background.setColor(color);
        background.setCornerRadius(dp(context, radius));
        return background;
    }

    public static TextView text(Context context, String value, int size, int color) {
        TextView view = new TextView(context);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(color);
        view.setFontFeatureSettings("kern");
        return view;
    }

    public static TextView title(Context context, String value, int size) {
        TextView view = text(context, value, size, TEXT);
        view.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        return view;
    }

    public static Button button(Context context, String value, boolean primary) {
        Button button = new Button(context);
        button.setText(value);
        button.setTextSize(14);
        button.setAllCaps(false);
        button.setTextColor(primary ? BACKGROUND : TEXT);
        button.setMinHeight(dp(context, 48));
        button.setMinimumHeight(dp(context, 48));
        button.setMinWidth(0);
        button.setMinimumWidth(0);
        button.setPadding(dp(context, 16), dp(context, 6), dp(context, 16), dp(context, 6));
        GradientDrawable surface = shape(context, primary ? ACCENT : SURFACE, 14);
        if (!primary) surface.setStroke(dp(context, 1), BORDER);
        button.setBackground(new RippleDrawable(ColorStateList.valueOf(0x33ffffff), surface, null));
        button.setStateListAnimator(null);
        return button;
    }

    public static LinearLayout column(Context context, int padding) {
        LinearLayout result = new LinearLayout(context);
        result.setOrientation(LinearLayout.VERTICAL);
        int p = dp(context, padding);
        result.setPadding(p, p, p, p);
        return result;
    }

    public static LinearLayout row(Context context) {
        LinearLayout result = new LinearLayout(context);
        result.setOrientation(LinearLayout.HORIZONTAL);
        result.setGravity(Gravity.CENTER_VERTICAL);
        return result;
    }

    public static void gap(LinearLayout parent, int height) {
        parent.addView(new View(parent.getContext()), new LinearLayout.LayoutParams(1, dp(parent.getContext(), height)));
    }

    public static void systemBars(Activity activity) {
        activity.getWindow().setStatusBarColor(BACKGROUND);
        activity.getWindow().setNavigationBarColor(BACKGROUND);
        activity.getWindow().getDecorView().setSystemUiVisibility(0);
    }
}
