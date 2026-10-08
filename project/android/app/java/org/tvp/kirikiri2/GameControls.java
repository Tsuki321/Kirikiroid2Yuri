package org.tvp.kirikiri2;

import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.os.Handler;
import android.os.Looper;
import android.view.ContextThemeWrapper;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import com.yuri.kirikiri2.LibraryActivity;
import com.yuri.kirikiri2.R;
import static org.tvp.kirikiri2.ModernUi.*;

/** Lightweight controls above the GL surface; empty areas pass touches to the game. */
public final class GameControls {
    private final KR2Activity activity;
    private final Context theme;
    private final SharedPreferences preferences;
    private final FrameLayout overlay;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final GameInput input;
    private final PointerView pointer;
    private AlertDialog panel;
    private boolean paused = true, active, keyboard;
    private int layout, size, opacity;
    private boolean quickKeys, leftHanded;

    public GameControls(KR2Activity activity, FrameLayout parent) {
        this.activity = activity;
        theme = new ContextThemeWrapper(activity, R.style.LibraryTheme);
        preferences = activity.getSharedPreferences("game_controls", Context.MODE_PRIVATE);
        layout = preferences.getInt("direction_layout", 0);
        size = preferences.getInt("button_size", 52);
        opacity = preferences.getInt("opacity", 85);
        quickKeys = preferences.getBoolean("quick_keys", false);
        leftHanded = preferences.getBoolean("left_handed", false);
        overlay = new FrameLayout(activity);
        overlay.setMotionEventSplittingEnabled(true);
        overlay.setClipChildren(false);
        parent.addView(overlay, new FrameLayout.LayoutParams(-1, -1));
        overlay.setOnApplyWindowInsetsListener((view, insets) -> {
            int left = insets.getSystemWindowInsetLeft(), right = insets.getSystemWindowInsetRight();
            int top = insets.getSystemWindowInsetTop(), bottom = insets.getSystemWindowInsetBottom();
            if (android.os.Build.VERSION.SDK_INT >= 28 && insets.getDisplayCutout() != null) {
                left = Math.max(left, insets.getDisplayCutout().getSafeInsetLeft());
                right = Math.max(right, insets.getDisplayCutout().getSafeInsetRight());
                top = Math.max(top, insets.getDisplayCutout().getSafeInsetTop());
                bottom = Math.max(bottom, insets.getDisplayCutout().getSafeInsetBottom());
            }
            overlay.setPadding(left, top, right, bottom);
            return insets;
        });
        overlay.requestApplyInsets();
        pointer = new PointerView(activity);
        input = new GameInput(activity.getGLSurfaceView(), GameInput.engine());
        input.setCursor((x, y) -> {
            pointer.x = x - overlay.getPaddingLeft(); pointer.y = y - overlay.getPaddingTop(); pointer.invalidate();
        });
        input.setTrackpad(preferences.getBoolean("trackpad", false));
        input.setWasdArrows(preferences.getBoolean("wasd_arrows", false));
        input.setSensitivity(preferences.getInt("sensitivity", 120) / 100f);
        GameInput.nativeEnableControls();
        keepAwake(preferences.getBoolean("keep_awake", true));
        rebuild();
    }

    public GameInput input() { return input; }

    private final Runnable refresh = new Runnable() {
        @Override public void run() {
            if (paused) return;
            boolean enabled = GameInput.nativeIsActive();
            if (enabled != active) {
                active = enabled;
                if (!active) input.release();
                overlay.setVisibility(active ? View.VISIBLE : View.GONE);
            }
            if (active && activity.hasWindowFocus() && !preferences.getBoolean("seen_guide", false)) {
                preferences.edit().putBoolean("seen_guide", true).apply();
                showGuide();
            }
            handler.postDelayed(this, 200);
        }
    };

    public void resume() { paused = false; handler.removeCallbacks(refresh); handler.post(refresh); }
    public void pause() { paused = true; handler.removeCallbacks(refresh); input.release(); }
    public void destroy() { pause(); if (panel != null) panel.dismiss(); handler.removeCallbacksAndMessages(null); }

    public void release() { input.release(); }

    private void keepAwake(boolean value) {
        activity.getGLSurfaceView().setKeepScreenOn(value);
        if (value) activity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else activity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    private FrameLayout.LayoutParams anchored(int width, int height, int gravity) {
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(width, height, gravity);
        int margin = dp(activity, 12);
        params.setMargins(margin, margin, margin, margin);
        return params;
    }

    private void rebuild() {
        input.release();
        overlay.removeAllViews();
        overlay.addView(pointer, new FrameLayout.LayoutParams(-1, -1));
        pointer.setVisibility(input.isTrackpad() ? View.VISIBLE : View.GONE);
        Button menu = button(theme, "Controls", false);
        menu.setContentDescription("Open game controls");
        menu.setOnClickListener(view -> showPanel());
        overlay.addView(menu, anchored(-2, dp(activity, 48), Gravity.TOP | (leftHanded ? Gravity.LEFT : Gravity.RIGHT)));

        if (layout != 0 && !keyboard) {
            LinearLayout directions = column(theme, 0);
            directions.setAlpha(opacity / 100f);
            String[][] labels = layout == 1 ? new String[][] {{"", "W", ""}, {"A", "S", "D"}}
                    : new String[][] {{"", "↑", ""}, {"←", "↓", "→"}};
            int[][] keys = layout == 1 ? new int[][] {{0, 'W', 0}, {'A', 'S', 'D'}}
                    : new int[][] {{0, 0x26, 0}, {0x25, 0x28, 0x27}};
            for (int row = 0; row < labels.length; ++row) {
                LinearLayout strip = ModernUi.row(theme);
                for (int col = 0; col < 3; ++col) {
                    View key = keys[row][col] == 0 ? new View(theme) : key(labels[row][col], keys[row][col]);
                    LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(dp(activity, size), dp(activity, size));
                    params.setMargins(dp(activity, 2), dp(activity, 2), dp(activity, 2), dp(activity, 2));
                    strip.addView(key, params);
                }
                directions.addView(strip);
            }
            overlay.addView(directions, anchored(-2, -2, Gravity.BOTTOM | (leftHanded ? Gravity.RIGHT : Gravity.LEFT)));
        }
        if (keyboard) addKeyboard();
        else if (quickKeys || input.isTrackpad()) {
            LinearLayout group = column(theme, 0);
            group.setAlpha(opacity / 100f);
            if (quickKeys) {
                LinearLayout strip = row(theme);
                addKey(strip, "Enter", 0x0d); addKey(strip, "Space", 0x20);
                group.addView(strip);
                strip = row(theme);
                addKey(strip, "Esc", 0x1b); addKey(strip, "Ctrl", 0x11);
                group.addView(strip);
            }
            if (input.isTrackpad()) {
                LinearLayout strip = row(theme);
                Button left = holdButton("Left", "Hold left mouse button", () -> input.mouseDown(0), () -> input.mouseUp(0, true), () -> input.mouseUp(0, false));
                Button right = holdButton("Right", "Right mouse button", () -> input.mouseDown(1), () -> input.mouseUp(1, true), () -> input.mouseUp(1, false));
                addWeighted(strip, left); addWeighted(strip, right); group.addView(strip);
            }
            overlay.addView(group, anchored(dp(activity, Math.max(156, size * 3)), -2, Gravity.BOTTOM | (leftHanded ? Gravity.LEFT : Gravity.RIGHT)));
        }
        overlay.setVisibility(active ? View.VISIBLE : View.GONE);
    }

    private void addWeighted(LinearLayout row, View view) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, dp(activity, size), 1);
        params.setMargins(dp(activity, 2), dp(activity, 2), dp(activity, 2), dp(activity, 2));
        row.addView(view, params);
    }

    private void addKey(LinearLayout row, String label, int code) { addWeighted(row, key(label, code)); }

    private Button key(String label, int code) {
        return holdButton(label, "Game key " + label, () -> input.keyDown(code), () -> input.keyUp(code), () -> input.keyUp(code));
    }

    private Button holdButton(String label, String description, Runnable down, Runnable up, Runnable cancel) {
        Button button = button(theme, label, false);
        button.setPadding(dp(activity, 4), 0, dp(activity, 4), 0);
        button.setContentDescription(description);
        final boolean[] held = {false}, completingTouch = {false};
        button.setOnClickListener(view -> { if (!completingTouch[0]) { down.run(); up.run(); } });
        button.setOnTouchListener((view, event) -> {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    held[0] = true; button.setPressed(true); down.run(); return true;
                case MotionEvent.ACTION_MOVE:
                    if (held[0] && (event.getX() < -dp(activity, 8) || event.getY() < -dp(activity, 8)
                            || event.getX() > view.getWidth() + dp(activity, 8) || event.getY() > view.getHeight() + dp(activity, 8))) {
                        held[0] = false; button.setPressed(false); cancel.run();
                    }
                    return true;
                case MotionEvent.ACTION_UP:
                    if (held[0]) { held[0] = false; up.run(); completingTouch[0] = true; view.performClick(); completingTouch[0] = false; }
                    button.setPressed(false); return true;
                case MotionEvent.ACTION_CANCEL:
                    if (held[0]) { held[0] = false; cancel.run(); }
                    button.setPressed(false); return true;
                default: return true;
            }
        });
        return button;
    }

    private void addKeyboard() {
        LinearLayout container = column(theme, 6);
        container.setBackground(shape(theme, BACKGROUND, 16));
        container.setAlpha(Math.max(0.85f, opacity / 100f));
        LinearLayout header = row(theme);
        TextView label = text(theme, "GAME KEYS", 11, ACCENT);
        header.addView(label, new LinearLayout.LayoutParams(0, -2, 1));
        Button text = button(theme, "Type text", false); text.setOnClickListener(view -> typeText());
        header.addView(text);
        Button hide = button(theme, "Hide keys", true); hide.setOnClickListener(view -> { keyboard = false; rebuild(); });
        LinearLayout.LayoutParams hideParams = new LinearLayout.LayoutParams(-2, dp(activity, 48));
        hideParams.leftMargin = dp(activity, 8);
        header.addView(hide, hideParams);
        container.addView(header, new LinearLayout.LayoutParams(-1, dp(activity, 48)));
        LinearLayout keyboardView = column(theme, 6);
        String[] rows = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
        for (String labels : rows) {
            LinearLayout strip = row(theme);
            for (char letter : labels.toCharArray()) addKey(strip, Character.toString(letter), letter);
            keyboardView.addView(strip);
        }
        LinearLayout special = row(theme);
        addKey(special, "Esc", 0x1b); addKey(special, "Tab", 0x09); addKey(special, "Shift", 0x10);
        addKey(special, "Ctrl", 0x11); addKey(special, "Space", 0x20); addKey(special, "⌫", 0x08); addKey(special, "Enter", 0x0d);
        keyboardView.addView(special);
        LinearLayout functions = row(theme);
        for (int i = 0; i < 12; ++i) addKey(functions, "F" + (i + 1), 0x70 + i);
        keyboardView.addView(functions);
        LinearLayout navigation = row(theme);
        addKey(navigation, "←", 0x25); addKey(navigation, "↑", 0x26); addKey(navigation, "↓", 0x28); addKey(navigation, "→", 0x27);
        addKey(navigation, "PgUp", 0x21); addKey(navigation, "PgDn", 0x22);
        keyboardView.addView(navigation);
        ScrollView scroll = new ScrollView(theme);
        scroll.addView(keyboardView);
        container.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        int height = Math.min(dp(activity, 380), Math.max(dp(activity, 180), activity.getResources().getDisplayMetrics().heightPixels * 3 / 4));
        overlay.addView(container, anchored(-1, height, Gravity.BOTTOM));
    }

    private void addAction(LinearLayout parent, String label, Runnable action) {
        Button button = button(theme, label, false);
        button.setOnClickListener(view -> { dismissPanel(); action.run(); });
        parent.addView(button, new LinearLayout.LayoutParams(-1, -2));
        gap(parent, 8);
    }

    public boolean showPanel() {
        if (!GameInput.nativeIsActive()) return false;
        input.release();
        if (panel != null && panel.isShowing()) return true;
        LinearLayout body = column(theme, 20);
        body.addView(text(theme, "Touch to click. Make the controls yours.", 14, MUTED));
        gap(body, 16);
        addAction(body, keyboard ? "Hide keyboard" : "Show keyboard", () -> { keyboard = !keyboard; rebuild(); });
        addAction(body, "Type text into the game", this::typeText);
        addAction(body, "Direction keys: " + new String[] {"Hidden", "WASD", "Arrows"}[Math.max(0, Math.min(layout, 2))], () -> {
            new AlertDialog.Builder(theme).setTitle("On-screen direction keys")
                    .setSingleChoiceItems(new String[] {"Hidden", "WASD", "Arrow keys"}, layout, (dialog, choice) -> {
                        layout = choice; preferences.edit().putInt("direction_layout", layout).apply(); dialog.dismiss(); rebuild();
                    }).setNegativeButton("Cancel", null).show();
        });
        check(body, "Show Enter, Space, Esc and Ctrl", quickKeys, value -> { quickKeys = value; preferences.edit().putBoolean("quick_keys", value).apply(); rebuild(); });
        check(body, "Touchpad mode", input.isTrackpad(), value -> { input.setTrackpad(value); preferences.edit().putBoolean("trackpad", value).apply(); rebuild(); });
        body.addView(text(theme, "Touchpad: slide anywhere to move the pointer; use Left to hold and drag.", 12, MUTED));
        check(body, "Map physical WASD to arrow keys", preferences.getBoolean("wasd_arrows", false), value -> { input.setWasdArrows(value); preferences.edit().putBoolean("wasd_arrows", value).apply(); });
        check(body, "Swap controls to the other side", leftHanded, value -> { leftHanded = value; preferences.edit().putBoolean("left_handed", value).apply(); rebuild(); });
        check(body, "Keep screen awake while playing", preferences.getBoolean("keep_awake", true), value -> { preferences.edit().putBoolean("keep_awake", value).apply(); keepAwake(value); });
        slider(body, "Button size", 44, 72, size, value -> { size = value; preferences.edit().putInt("button_size", size).apply(); rebuild(); });
        slider(body, "Control opacity", 45, 100, opacity, value -> { opacity = value; preferences.edit().putInt("opacity", opacity).apply(); rebuild(); });
        slider(body, "Pointer speed", 50, 250, preferences.getInt("sensitivity", 120), value -> { preferences.edit().putInt("sensitivity", value).apply(); input.setSensitivity(value / 100f); });
        gap(body, 12);
        addAction(body, "Touch and keyboard guide", this::showGuide);
        addAction(body, "Game preferences", activity::showGamePreferences);
        addAction(body, "Advanced engine options", () -> { input.release(); KR2Activity.nativeKeyAction(android.view.KeyEvent.KEYCODE_MENU, true); KR2Activity.nativeKeyAction(android.view.KeyEvent.KEYCODE_MENU, false); });
        addAction(body, "Return to library", this::confirmExit);
        ScrollView scroll = new ScrollView(theme); scroll.addView(body);
        panel = new AlertDialog.Builder(theme).setTitle("Game controls").setView(scroll).setPositiveButton("Resume game", null).create();
        panel.setOnDismissListener(dialog -> { input.release(); activity.getGLSurfaceView().requestFocus(); });
        panel.show();
        return true;
    }

    private interface Change { void apply(boolean value); }
    private interface NumberChange { void apply(int value); }

    private void check(LinearLayout parent, String label, boolean value, Change changed) {
        CheckBox check = new CheckBox(theme);
        check.setText(label); check.setTextColor(TEXT); check.setChecked(value); check.setMinHeight(dp(activity, 48));
        check.setOnCheckedChangeListener((button, checked) -> changed.apply(checked));
        parent.addView(check, new LinearLayout.LayoutParams(-1, -2));
    }

    private void slider(LinearLayout parent, String name, int min, int max, int value, NumberChange changed) {
        TextView label = text(theme, name + " · " + value, 13, MUTED);
        gap(parent, 12); parent.addView(label);
        SeekBar slider = new SeekBar(theme);
        slider.setContentDescription(name); slider.setMax(max - min); slider.setProgress(value - min);
        slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onStartTrackingTouch(SeekBar bar) { }
            public void onProgressChanged(SeekBar bar, int progress, boolean user) { label.setText(name + " · " + (progress + min)); }
            public void onStopTrackingTouch(SeekBar bar) { changed.apply(bar.getProgress() + min); }
        });
        parent.addView(slider, new LinearLayout.LayoutParams(-1, dp(activity, 48)));
    }

    private void dismissPanel() { if (panel != null) { panel.dismiss(); panel = null; } }

    public void showGuide() {
        input.release();
        new AlertDialog.Builder(theme).setTitle("A few easy controls")
                .setMessage("Tap: left click or advance dialogue.\nDrag: touch and slide without lifting.\nHold still: right click, often the game menu.\nTwo-finger tap: right click.\nTwo-finger slide: mouse wheel or backlog.\nThree-finger tap: middle click.\n\n"
                        + "Touchpad mode moves a visible pointer relative to your finger. Tap to click; hold the Left button while sliding to drag.\n\n"
                        + "Connect a keyboard for letters, WASD, arrows, function keys and shortcuts. Use Show keyboard for on-screen game keys, or Type text for names and other text.\n\n"
                        + "Android Back opens Controls. Esc is sent to the game. Save in the game before returning to your library.")
                .setPositiveButton("Let’s play", null).show();
    }

    private void typeText() {
        input.release();
        EditText edit = new EditText(theme);
        edit.setHint("Text to send to the game");
        edit.setTextColor(TEXT); edit.setHintTextColor(MUTED);
        edit.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
        LinearLayout box = column(theme, 20); box.addView(edit, new LinearLayout.LayoutParams(-1, -2));
        box.addView(text(theme, "Sends text to the game’s currently focused field.", 13, MUTED));
        AlertDialog dialog = new AlertDialog.Builder(theme).setTitle("Type in the game").setView(box)
                .setNegativeButton("Cancel", null).setPositiveButton("Send text", (d, which) -> {
                    String value = edit.getText().toString();
                    if (!value.isEmpty()) KR2Activity.nativeCommitText(value, 1);
                }).create();
        dialog.setOnShowListener(d -> {
            edit.requestFocus();
            dialog.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
            ((InputMethodManager)activity.getSystemService(Context.INPUT_METHOD_SERVICE)).showSoftInput(edit, InputMethodManager.SHOW_IMPLICIT);
        });
        dialog.show();
    }

    private void confirmExit() {
        new AlertDialog.Builder(theme).setTitle("Return to your library?")
                .setMessage("Save from the game’s menu first. Unsaved progress will be lost when the game closes.")
                .setNegativeButton("Keep playing", null).setPositiveButton("Return to library", (dialog, which) -> {
                    input.release();
                    Intent intent = new Intent(activity, LibraryActivity.class);
                    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
                    activity.startActivity(intent);
                    activity.finishAndRemoveTask();
                }).show();
    }

    private static final class PointerView extends View {
        float x, y;
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        PointerView(Context context) { super(context); setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO); }
        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            float scale = getResources().getDisplayMetrics().density;
            Path arrow = new Path(); arrow.moveTo(x, y); arrow.lineTo(x + 4 * scale, y + 20 * scale);
            arrow.lineTo(x + 8 * scale, y + 13 * scale); arrow.lineTo(x + 16 * scale, y + 12 * scale); arrow.close();
            paint.setStyle(Paint.Style.FILL); paint.setColor(TEXT); canvas.drawPath(arrow, paint);
            paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(2 * scale); paint.setColor(BACKGROUND); canvas.drawPath(arrow, paint);
        }
    }
}
