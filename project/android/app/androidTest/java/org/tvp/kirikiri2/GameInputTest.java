package org.tvp.kirikiri2;

import android.os.SystemClock;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import org.junit.Test;
import org.junit.runner.RunWith;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public class GameInputTest {
    private static final class Recorder implements GameInput.Sink {
        boolean active = true;
        final List<String> keys = new ArrayList<>(), text = new ArrayList<>();
        final List<int[]> pointer = new ArrayList<>();
        int releases;
        public boolean active() { return active; }
        public void key(int key, boolean down, boolean repeat) { keys.add(key + ":" + down + ":" + repeat); }
        public void text(String value) { text.add(value); }
        public void pointer(int action, int button, float x, float y, float scroll) {
            pointer.add(new int[] {action, button, (int)x, (int)y, (int)scroll});
        }
        public void release() { releases++; }
        int count(int action, int button) {
            int result = 0;
            for (int[] event : pointer) if (event[0] == action && event[1] == button) ++result;
            return result;
        }
    }
    private View surface;
    private GameInput input;
    private Recorder sink;

    private void main(Runnable test) {
        AtomicReference<Throwable> failure = new AtomicReference<>();
        InstrumentationRegistry.getInstrumentation().runOnMainSync(() -> {
            try { test.run(); } catch (Throwable error) { failure.set(error); }
        });
        if (failure.get() != null) throw new AssertionError(failure.get());
    }

    private void setup() {
        sink = new Recorder();
        surface = new View(InstrumentationRegistry.getInstrumentation().getTargetContext());
        input = new GameInput(surface, sink);
        surface.layout(0, 0, 1000, 600);
    }

    private void touch(int action, int[] ids, float[] xs, float[] ys) {
        MotionEvent.PointerProperties[] properties = new MotionEvent.PointerProperties[ids.length];
        MotionEvent.PointerCoords[] points = new MotionEvent.PointerCoords[ids.length];
        for (int i = 0; i < ids.length; ++i) {
            properties[i] = new MotionEvent.PointerProperties(); properties[i].id = ids[i]; properties[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
            points[i] = new MotionEvent.PointerCoords(); points[i].x = xs[i]; points[i].y = ys[i]; points[i].pressure = 1;
        }
        long now = SystemClock.uptimeMillis();
        MotionEvent event = MotionEvent.obtain(now, now, action, ids.length, properties, points, 0, 0, 1, 1, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0);
        assertTrue(input.onTouch(surface, event)); event.recycle();
    }

    private void finger(int action, float x, float y) { touch(action, new int[] {7}, new float[] {x}, new float[] {y}); }

    @Test public void keyboardChordsRepeatAndSharedHoldsReleaseExactlyOnce() { main(() -> {
        setup();
        input.onKey(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_SHIFT_LEFT));
        input.onKey(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_SHIFT_RIGHT));
        input.onKey(new KeyEvent(1, 1, KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_W, 0, KeyEvent.META_SHIFT_ON));
        input.onKey(new KeyEvent(1, 2, KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_W, 1, KeyEvent.META_SHIFT_ON));
        input.keyDown('W'); // An on-screen key shares the same physical key state.
        input.onKey(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_W));
        input.onKey(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_SHIFT_LEFT));
        assertEquals(java.util.Arrays.asList("16:true:false", "87:true:false", "87:true:true"), sink.keys);
        assertEquals(java.util.Arrays.asList("W", "W"), sink.text);
        input.keyUp('W');
        input.onKey(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_SHIFT_RIGHT));
        assertEquals("87:false:false", sink.keys.get(3));
        assertEquals("16:false:false", sink.keys.get(4));
    }); }

    @Test public void remappingDoesNotTypeLettersAndFocusLossClearsState() { main(() -> {
        setup(); input.setWasdArrows(true);
        input.onKey(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_W));
        input.onKey(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_D));
        input.release();
        assertEquals(java.util.Arrays.asList("38:true:false", "39:true:false", "38:false:false", "39:false:false"), sink.keys);
        assertTrue(sink.text.isEmpty());
        assertTrue("Canceled key-up must not leak to the old input handler", input.onKey(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_W)));
        assertFalse(input.onKey(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_VOLUME_UP)));
        assertFalse(input.onKey(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BACK)));
        assertTrue(input.onKey(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_ESCAPE)));
        input.release();
        sink.active = false;
        assertFalse(input.onKey(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_A)));
    }); }

    @Test public void tapClicksOnceAndDragCancellationNeverClicks() { main(() -> {
        setup();
        finger(MotionEvent.ACTION_DOWN, 200, 150); finger(MotionEvent.ACTION_UP, 200, 150);
        assertEquals(1, sink.count(0, 0)); assertEquals(1, sink.count(4, 0)); assertEquals(1, sink.count(1, 0));
        sink.pointer.clear();
        finger(MotionEvent.ACTION_DOWN, 200, 150); finger(MotionEvent.ACTION_MOVE, 400, 250);
        finger(MotionEvent.ACTION_CANCEL, 400, 250);
        assertEquals(1, sink.count(0, 0)); assertEquals(0, sink.count(4, 0)); assertEquals(1, sink.count(1, 0));
        int[] release = sink.pointer.get(sink.pointer.size() - 1);
        assertArrayEquals(new int[] {1, 0, 400, 250, 0}, release);
    }); }

    @Test public void twoFingerTapUsesPointerIdsAndDoesNotLeftClick() { main(() -> {
        setup();
        finger(MotionEvent.ACTION_DOWN, 200, 150);
        touch(MotionEvent.ACTION_POINTER_DOWN | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT), new int[] {7, 21}, new float[] {200, 300}, new float[] {150, 160});
        // The original finger lifts first; the remaining pointer must stay part of the same gesture.
        touch(MotionEvent.ACTION_POINTER_UP, new int[] {7, 21}, new float[] {200, 300}, new float[] {150, 160});
        touch(MotionEvent.ACTION_UP, new int[] {21}, new float[] {300}, new float[] {160});
        assertEquals(0, sink.count(0, 0)); assertEquals(1, sink.count(0, 1)); assertEquals(1, sink.count(1, 1));
    }); }

    @Test public void twoFingerScrollDoesNotClickAndTrackpadMovementIsRelative() { main(() -> {
        setup();
        finger(MotionEvent.ACTION_DOWN, 200, 150);
        touch(MotionEvent.ACTION_POINTER_DOWN | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT), new int[] {7, 21}, new float[] {200, 300}, new float[] {150, 160});
        touch(MotionEvent.ACTION_MOVE, new int[] {7, 21}, new float[] {200, 300}, new float[] {400, 410});
        touch(MotionEvent.ACTION_POINTER_UP | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT), new int[] {7, 21}, new float[] {200, 300}, new float[] {400, 410});
        finger(MotionEvent.ACTION_UP, 200, 400);
        assertTrue(sink.count(5, 0) > 0); assertEquals(0, sink.count(0, 0)); assertEquals(0, sink.count(0, 1));
        sink.pointer.clear(); input.setTrackpad(true); input.setSensitivity(1);
        finger(MotionEvent.ACTION_DOWN, 100, 100); finger(MotionEvent.ACTION_MOVE, 200, 120); finger(MotionEvent.ACTION_UP, 200, 120);
        assertEquals(0, sink.count(0, 0));
        int[] movement = sink.pointer.get(sink.pointer.size() - 1);
        assertArrayEquals(new int[] {2, 0, 600, 320, 0}, movement);
    }); }

    @Test public void longPressRightClicksOnceWithoutAnInitialLeftPress() {
        main(() -> { setup(); finger(MotionEvent.ACTION_DOWN, 200, 150); });
        SystemClock.sleep(ViewConfiguration.getLongPressTimeout() + 120);
        main(() -> {
            finger(MotionEvent.ACTION_UP, 200, 150);
            assertEquals(0, sink.count(0, 0)); assertEquals(1, sink.count(0, 1)); assertEquals(1, sink.count(1, 1));
        });
    }

    @Test public void holdingTheTouchpadMouseButtonCanDragWithoutAnExtraClick() { main(() -> {
        setup(); input.setTrackpad(true);
        input.mouseDown(0);
        finger(MotionEvent.ACTION_DOWN, 100, 100); finger(MotionEvent.ACTION_MOVE, 300, 150);
        finger(MotionEvent.ACTION_UP, 300, 150);
        input.mouseUp(0, true);
        assertEquals(1, sink.count(0, 0)); assertEquals(1, sink.count(1, 0)); assertEquals(0, sink.count(4, 0));
    }); }

    @Test public void aTwoFingerSwipeDoesNotTurnIntoARightClick() { main(() -> {
        setup(); finger(MotionEvent.ACTION_DOWN, 200, 150);
        touch(MotionEvent.ACTION_POINTER_DOWN | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT), new int[] {7, 21}, new float[] {200, 300}, new float[] {150, 160});
        touch(MotionEvent.ACTION_MOVE, new int[] {7, 21}, new float[] {400, 500}, new float[] {150, 160});
        touch(MotionEvent.ACTION_POINTER_UP | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT), new int[] {7, 21}, new float[] {400, 500}, new float[] {150, 160});
        finger(MotionEvent.ACTION_UP, 400, 150);
        assertEquals(0, sink.count(0, 0)); assertEquals(0, sink.count(0, 1));
    }); }

    @Test public void mouseWheelCarriesItsLocationAndButtonsAreNotDuplicated() { main(() -> {
        setup();
        MotionEvent.PointerProperties property = new MotionEvent.PointerProperties(); property.id = 0; property.toolType = MotionEvent.TOOL_TYPE_MOUSE;
        MotionEvent.PointerCoords point = new MotionEvent.PointerCoords(); point.x = 420; point.y = 240;
        int[] actions = {MotionEvent.ACTION_DOWN, MotionEvent.ACTION_BUTTON_PRESS, MotionEvent.ACTION_BUTTON_RELEASE, MotionEvent.ACTION_UP, MotionEvent.ACTION_SCROLL};
        int[] buttons = {MotionEvent.BUTTON_PRIMARY, MotionEvent.BUTTON_PRIMARY, 0, 0, 0};
        for (int i = 0; i < actions.length; ++i) {
            point.setAxisValue(MotionEvent.AXIS_VSCROLL, i == 4 ? 2 : 0);
            long now = SystemClock.uptimeMillis();
            MotionEvent event = MotionEvent.obtain(now, now, actions[i], 1, new MotionEvent.PointerProperties[] {property}, new MotionEvent.PointerCoords[] {point}, 0, buttons[i], 1, 1, 0, 0, InputDevice.SOURCE_MOUSE, 0);
            assertTrue(input.onGenericMotion(surface, event)); event.recycle();
        }
        assertEquals(1, sink.count(0, 0)); assertEquals(1, sink.count(1, 0)); assertEquals(1, sink.count(4, 0));
        assertArrayEquals(new int[] {5, 0, 420, 240, 2}, sink.pointer.get(sink.pointer.size() - 1));
    }); }
}
