package org.tvp.kirikiri2;

import android.os.Handler;
import android.os.Looper;
import android.util.SparseIntArray;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.KeyCharacterMap;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/** Routes Android input to Windows virtual keys and mouse events on the game thread. */
public final class GameInput implements View.OnTouchListener, View.OnGenericMotionListener {
    public interface Sink {
        boolean active();
        void key(int key, boolean down, boolean repeat);
        void pointer(int action, int button, float x, float y, float scroll);
        void text(String text);
        void release();
    }
    public interface Cursor { void move(float x, float y); }

    public static native boolean nativeIsActive();
    public static native void nativeEnableControls();
    private static native void nativeKey(int key, boolean down, boolean repeat);
    private static native void nativePointer(int action, int button, float x, float y, float scroll);
    private static native void nativeRelease();

    public static Sink engine() {
        return new Sink() {
            public boolean active() { return nativeIsActive(); }
            public void key(int key, boolean down, boolean repeat) { nativeKey(key, down, repeat); }
            public void pointer(int action, int button, float x, float y, float scroll) { nativePointer(action, button, x, y, scroll); }
            public void text(String text) { KR2Activity.nativeCommitText(text, 1); }
            public void release() { nativeRelease(); }
        };
    }

    private final View surface;
    private final Sink sink;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final SparseIntArray heldKeys = new SparseIntArray();
    private final Map<Long, Integer> physicalKeys = new HashMap<>();
    private final Set<Long> cancelledKeys = new HashSet<>();
    private final int[] mouseHolds = new int[3];
    private final float[] buttonX = new float[3], buttonY = new float[3];
    private final boolean[] buttonMoved = new boolean[3];
    private final boolean[] physicalMouse = new boolean[3];
    private final float slop;
    private boolean trackpad, wasdArrows, touch, moved, multi, scrolled, longClick, drag;
    private int primaryId = -1, deadAccent, touchButton;
    private float originX, originY, lastX, lastY, scrollY, pointerX, pointerY;
    private float mouseOriginX, mouseOriginY;
    private boolean mouseMoved;
    private Cursor cursor;
    private float sensitivity = 1.2f;

    public GameInput(View surface, Sink sink) {
        this.surface = surface;
        this.sink = sink;
        slop = ViewConfiguration.get(surface.getContext()).getScaledTouchSlop();
        surface.setOnTouchListener(this);
        surface.setOnGenericMotionListener(this);
        surface.addOnLayoutChangeListener((view, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
            if (oldRight == oldLeft || oldBottom == oldTop) position(view.getWidth() / 2f, view.getHeight() / 2f);
            else position(pointerX, pointerY);
        });
    }

    public void setCursor(Cursor value) { cursor = value; }
    public boolean isTrackpad() { return trackpad; }
    public void setTrackpad(boolean value) { release(); trackpad = value; position(surface.getWidth() / 2f, surface.getHeight() / 2f); }
    public void setWasdArrows(boolean value) { release(); wasdArrows = value; }
    public void setSensitivity(float value) { sensitivity = Math.max(0.5f, Math.min(2.5f, value)); }

    private void position(float x, float y) {
        pointerX = Math.max(0, Math.min(Math.max(0, surface.getWidth() - 1), x));
        pointerY = Math.max(0, Math.min(Math.max(0, surface.getHeight() - 1), y));
        for (int button = 0; button < 3; ++button)
            if (mouseHolds[button] > 0 && Math.hypot(pointerX - buttonX[button], pointerY - buttonY[button]) > slop)
                buttonMoved[button] = true;
        if (cursor != null) cursor.move(pointerX, pointerY);
    }

    private void pointer(int action, int button, float scroll) { sink.pointer(action, button, pointerX, pointerY, scroll); }

    public void keyDown(int key) {
        if (!sink.active()) return;
        int count = heldKeys.get(key);
        heldKeys.put(key, count + 1);
        if (count == 0) sink.key(key, true, false);
    }

    public void keyUp(int key) {
        int count = heldKeys.get(key);
        if (count <= 0) return;
        if (count == 1) { heldKeys.delete(key); sink.key(key, false, false); }
        else heldKeys.put(key, count - 1);
    }

    public void tapKey(int key) { keyDown(key); keyUp(key); }

    public void mouseDown(int button) {
        if (!sink.active()) return;
        if (mouseHolds[button]++ == 0) {
            buttonX[button] = pointerX; buttonY[button] = pointerY; buttonMoved[button] = false;
            pointer(0, button, 0);
        }
    }

    public void mouseUp(int button, boolean click) {
        if (mouseHolds[button] == 0) return;
        if (--mouseHolds[button] == 0) {
            if (click && !buttonMoved[button]) pointer(4, button, 0);
            pointer(1, button, 0);
        }
    }

    public void click(int button) { mouseDown(button); mouseUp(button, true); }
    public void scroll(float amount) { if (sink.active()) pointer(5, 0, amount); }

    private final Runnable hold = this::longPress;

    private void longPress() {
        if (touch && !moved && !multi && sink.active()) {
            longClick = true;
            click(1);
            surface.performHapticFeedback(android.view.HapticFeedbackConstants.LONG_PRESS);
        }
    }

    @Override public boolean onTouch(View view, MotionEvent event) {
        if (event.isFromSource(InputDevice.SOURCE_MOUSE)) return mouse(event);
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            if (!sink.active()) return false;
            touch = true; moved = multi = scrolled = longClick = drag = false;
            touchButton = 0;
            primaryId = event.getPointerId(0);
            originX = lastX = event.getX(); originY = lastY = event.getY();
            if (!trackpad) position(lastX, lastY);
            pointer(2, 0, 0);
            handler.postDelayed(hold, ViewConfiguration.getLongPressTimeout());
            return true;
        }
        if (!touch) return false;
        if (!sink.active() || action == MotionEvent.ACTION_CANCEL) { cancelTouch(); return true; }
        if (action == MotionEvent.ACTION_POINTER_DOWN) {
            handler.removeCallbacks(hold);
            multi = true;
            touchButton = event.getPointerCount() >= 3 ? 2 : 1;
            if (drag) { mouseUp(0, false); drag = false; }
            originX = averageX(event); originY = lastY = averageY(event); scrollY = 0;
            return true;
        }
        if (action == MotionEvent.ACTION_POINTER_UP) {
            // Once a two-finger gesture starts, no remaining finger can produce a left click.
            lastY = averageY(event);
            return true;
        }
        int index = event.findPointerIndex(primaryId);
        if (action == MotionEvent.ACTION_MOVE && multi) {
            if (event.getPointerCount() >= 2) {
                float y = averageY(event);
                if (Math.hypot(averageX(event) - originX, y - originY) > slop) moved = true;
                scrollY += y - lastY; lastY = y;
                float step = ModernUi.dp(view.getContext(), 24);
                int steps = (int)(scrollY / step);
                if (steps != 0) { scrolled = true; scroll(steps); scrollY -= steps * step; }
            }
            return true;
        }
        if (action == MotionEvent.ACTION_MOVE && index >= 0 && !longClick) {
            float x = event.getX(index), y = event.getY(index);
            if (!moved && Math.hypot(x - originX, y - originY) > slop) {
                moved = true; handler.removeCallbacks(hold);
                if (!trackpad) { position(originX, originY); mouseDown(0); drag = true; }
            }
            if (trackpad) position(pointerX + (x - lastX) * sensitivity, pointerY + (y - lastY) * sensitivity);
            else position(x, y);
            lastX = x; lastY = y;
            pointer(2, 0, 0);
        } else if (action == MotionEvent.ACTION_UP) {
            handler.removeCallbacks(hold);
            if (!multi && !trackpad && index >= 0) position(event.getX(index), event.getY(index));
            if (drag) mouseUp(0, false);
            else if (!longClick && !scrolled && !moved) click(touchButton);
            touch = false; drag = false; primaryId = -1;
            view.performClick();
        }
        return true;
    }

    private float averageY(MotionEvent event) {
        float result = 0;
        for (int i = 0; i < event.getPointerCount(); ++i) result += event.getY(i);
        return result / event.getPointerCount();
    }

    private float averageX(MotionEvent event) {
        float result = 0;
        for (int i = 0; i < event.getPointerCount(); ++i) result += event.getX(i);
        return result / event.getPointerCount();
    }

    private void cancelTouch() {
        handler.removeCallbacks(hold);
        if (drag) mouseUp(0, false);
        touch = drag = false; primaryId = -1;
    }

    private boolean mouse(MotionEvent event) {
        if (!sink.active()) return false;
        int action = event.getActionMasked();
        position(event.getX(), event.getY());
        if (action == MotionEvent.ACTION_SCROLL) { scroll(event.getAxisValue(MotionEvent.AXIS_VSCROLL)); return true; }
        if (action == MotionEvent.ACTION_CANCEL) {
            for (int i = 0; i < 3; ++i) { if (physicalMouse[i]) mouseUp(i, false); physicalMouse[i] = false; }
            return true;
        }
        if (action != MotionEvent.ACTION_DOWN && action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_MOVE
                && action != MotionEvent.ACTION_HOVER_MOVE && action != MotionEvent.ACTION_BUTTON_PRESS
                && action != MotionEvent.ACTION_BUTTON_RELEASE) return false;
        int state = event.getButtonState();
        if (action == MotionEvent.ACTION_DOWN && state == 0) state = MotionEvent.BUTTON_PRIMARY;
        if (action == MotionEvent.ACTION_UP) state = 0;
        if (Math.hypot(pointerX - mouseOriginX, pointerY - mouseOriginY) > slop) mouseMoved = true;
        int[] flags = {MotionEvent.BUTTON_PRIMARY, MotionEvent.BUTTON_SECONDARY, MotionEvent.BUTTON_TERTIARY};
        for (int i = 0; i < flags.length; ++i) {
            boolean down = (state & flags[i]) != 0;
            if (down == physicalMouse[i]) continue;
            if (down) { mouseOriginX = pointerX; mouseOriginY = pointerY; mouseMoved = false; mouseDown(i); }
            else mouseUp(i, !mouseMoved);
            physicalMouse[i] = down;
        }
        pointer(2, 0, 0);
        return true;
    }

    @Override public boolean onGenericMotion(View view, MotionEvent event) {
        if (event.isFromSource(InputDevice.SOURCE_MOUSE)) return mouse(event);
        return false;
    }

    public boolean onKey(KeyEvent event) {
        long physical = ((long)event.getDeviceId() << 32) | (event.getKeyCode() & 0xffffffffL);
        if (event.getAction() == KeyEvent.ACTION_UP) {
            Integer held = physicalKeys.remove(physical);
            if (held == null) return cancelledKeys.remove(physical);
            keyUp(held);
            return true;
        }
        if (!sink.active()) return false;
        if (event.getAction() == KeyEvent.ACTION_MULTIPLE && event.getCharacters() != null) {
            sink.text(event.getCharacters()); return true;
        }
        if (event.getAction() != KeyEvent.ACTION_DOWN) return false;
        cancelledKeys.remove(physical);
        int key = windowsKey(event.getKeyCode());
        if (key == 0) return false;
        if (wasdArrows) {
            if (key == 'W') key = 0x26; else if (key == 'A') key = 0x25;
            else if (key == 'S') key = 0x28; else if (key == 'D') key = 0x27;
        }
        Integer held = physicalKeys.get(physical);
        if (held == null) { physicalKeys.put(physical, key); keyDown(key); }
        else if (event.getRepeatCount() > 0) sink.key(held, true, true);
        if (!event.isCtrlPressed() && !event.isAltPressed() && !event.isMetaPressed()
                && (!wasdArrows || key < 0x25 || key > 0x28)) {
            int unicode = event.getUnicodeChar();
            if ((unicode & KeyCharacterMap.COMBINING_ACCENT) != 0) deadAccent = unicode & KeyCharacterMap.COMBINING_ACCENT_MASK;
            else if (unicode >= 32 && Character.isValidCodePoint(unicode)) {
                if (deadAccent != 0) {
                    int combined = KeyCharacterMap.getDeadChar(deadAccent, unicode);
                    if (combined != 0) unicode = combined;
                    else sink.text(new String(Character.toChars(deadAccent)));
                    deadAccent = 0;
                }
                sink.text(new String(Character.toChars(unicode)));
            }
        }
        return true;
    }

    public void release() {
        cancelTouch();
        for (int i = 0; i < heldKeys.size(); ++i) sink.key(heldKeys.keyAt(i), false, false);
        heldKeys.clear(); cancelledKeys.addAll(physicalKeys.keySet()); physicalKeys.clear(); deadAccent = 0;
        for (int i = 0; i < 3; ++i) { mouseHolds[i] = 0; physicalMouse[i] = false; }
        sink.release();
    }

    /** Stable Win32 VK values used by Kirikiri, independent of Cocos key enums. */
    public static int windowsKey(int key) {
        if (key >= KeyEvent.KEYCODE_A && key <= KeyEvent.KEYCODE_Z) return 0x41 + key - KeyEvent.KEYCODE_A;
        if (key >= KeyEvent.KEYCODE_0 && key <= KeyEvent.KEYCODE_9) return 0x30 + key - KeyEvent.KEYCODE_0;
        if (key >= KeyEvent.KEYCODE_F1 && key <= KeyEvent.KEYCODE_F12) return 0x70 + key - KeyEvent.KEYCODE_F1;
        if (key >= KeyEvent.KEYCODE_NUMPAD_0 && key <= KeyEvent.KEYCODE_NUMPAD_9) return 0x60 + key - KeyEvent.KEYCODE_NUMPAD_0;
        switch (key) {
            case KeyEvent.KEYCODE_DPAD_LEFT: return 0x25;
            case KeyEvent.KEYCODE_DPAD_UP: return 0x26;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return 0x27;
            case KeyEvent.KEYCODE_DPAD_DOWN: return 0x28;
            case KeyEvent.KEYCODE_ENTER: case KeyEvent.KEYCODE_NUMPAD_ENTER: case KeyEvent.KEYCODE_DPAD_CENTER: return 0x0d;
            case KeyEvent.KEYCODE_ESCAPE: return 0x1b;
            case KeyEvent.KEYCODE_DEL: return 0x08;
            case KeyEvent.KEYCODE_FORWARD_DEL: return 0x2e;
            case KeyEvent.KEYCODE_TAB: return 0x09;
            case KeyEvent.KEYCODE_SPACE: return 0x20;
            case KeyEvent.KEYCODE_CTRL_LEFT: case KeyEvent.KEYCODE_CTRL_RIGHT: return 0x11;
            case KeyEvent.KEYCODE_SHIFT_LEFT: case KeyEvent.KEYCODE_SHIFT_RIGHT: return 0x10;
            case KeyEvent.KEYCODE_ALT_LEFT: case KeyEvent.KEYCODE_ALT_RIGHT: return 0x12;
            case KeyEvent.KEYCODE_META_LEFT: return 0x5b;
            case KeyEvent.KEYCODE_META_RIGHT: return 0x5c;
            case KeyEvent.KEYCODE_PAGE_UP: return 0x21;
            case KeyEvent.KEYCODE_PAGE_DOWN: return 0x22;
            case KeyEvent.KEYCODE_MOVE_HOME: return 0x24;
            case KeyEvent.KEYCODE_MOVE_END: return 0x23;
            case KeyEvent.KEYCODE_INSERT: return 0x2d;
            case KeyEvent.KEYCODE_CAPS_LOCK: return 0x14;
            case KeyEvent.KEYCODE_NUM_LOCK: return 0x90;
            case KeyEvent.KEYCODE_SCROLL_LOCK: return 0x91;
            case KeyEvent.KEYCODE_BREAK: return 0x13;
            case KeyEvent.KEYCODE_SYSRQ: return 0x2c;
            case KeyEvent.KEYCODE_MINUS: return 0xbd;
            case KeyEvent.KEYCODE_EQUALS: return 0xbb;
            case KeyEvent.KEYCODE_COMMA: return 0xbc;
            case KeyEvent.KEYCODE_PERIOD: return 0xbe;
            case KeyEvent.KEYCODE_SEMICOLON: return 0xba;
            case KeyEvent.KEYCODE_APOSTROPHE: return 0xde;
            case KeyEvent.KEYCODE_SLASH: return 0xbf;
            case KeyEvent.KEYCODE_BACKSLASH: return 0xdc;
            case KeyEvent.KEYCODE_LEFT_BRACKET: return 0xdb;
            case KeyEvent.KEYCODE_RIGHT_BRACKET: return 0xdd;
            case KeyEvent.KEYCODE_GRAVE: return 0xc0;
            case KeyEvent.KEYCODE_NUMPAD_MULTIPLY: return 0x6a;
            case KeyEvent.KEYCODE_NUMPAD_ADD: return 0x6b;
            case KeyEvent.KEYCODE_NUMPAD_SUBTRACT: return 0x6d;
            case KeyEvent.KEYCODE_NUMPAD_DOT: return 0x6e;
            case KeyEvent.KEYCODE_NUMPAD_DIVIDE: return 0x6f;
            case KeyEvent.KEYCODE_NUMPAD_COMMA: return 0x6c;
            default: return 0;
        }
    }
}
