package org.tvp.kirikiri2;

import android.app.Instrumentation;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.SystemClock;
import android.provider.DocumentsContract;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.accessibility.AccessibilityNodeInfo;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import com.yuri.kirikiri2.LibraryActivity;
import com.yuri.kirikiri2.MainActivity;
import org.junit.Test;
import org.junit.runner.RunWith;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import static org.junit.Assert.*;

/** Runs separately because the legacy engine deliberately exits its process on activity destruction. */
@RunWith(AndroidJUnit4.class)
public class NativeControlsTest {
    private final Instrumentation instrumentation = InstrumentationRegistry.getInstrumentation();
    private File events;
    private float x, y;
    private long downTime;

    private static byte[] bytes(InputStream input) throws Exception {
        try (InputStream stream = input; ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096]; int count;
            while ((count = stream.read(buffer)) >= 0) output.write(buffer, 0, count);
            return output.toByteArray();
        }
    }

    private String log() throws Exception {
        if (!events.exists()) return "";
        byte[] data = bytes(new FileInputStream(events));
        if (data.length >= 2 && (data[0] & 255) == 255 && (data[1] & 255) == 254)
            return new String(data, 2, data.length - 2, StandardCharsets.UTF_16LE);
        return new String(data, StandardCharsets.UTF_8);
    }

    private void waitLog(String text) throws Exception {
        long deadline = SystemClock.uptimeMillis() + 20000;
        while (!log().contains(text) && SystemClock.uptimeMillis() < deadline) SystemClock.sleep(80);
        assertTrue("Engine event " + text + "\n" + log(), log().contains(text));
    }

    private int count(String prefix) throws Exception {
        int count = 0; for (String line : log().split("\\r?\\n")) if (line.startsWith(prefix)) ++count; return count;
    }

    private void waitCount(String prefix, int expected) throws Exception {
        long deadline = SystemClock.uptimeMillis() + 12000;
        while (count(prefix) < expected && SystemClock.uptimeMillis() < deadline) SystemClock.sleep(80);
        assertEquals("Engine event count: " + prefix, expected, count(prefix));
    }

    private void key(int code, boolean down, int meta) {
        long now = SystemClock.uptimeMillis();
        assertTrue(UiChecks.automation().injectInputEvent(new KeyEvent(now, now,
                down ? KeyEvent.ACTION_DOWN : KeyEvent.ACTION_UP, code, 0, meta,
                -1, 0, 0, InputDevice.SOURCE_KEYBOARD), true));
    }

    private void touch(int action, int[] ids, float[] xs, float[] ys) {
        if (action == MotionEvent.ACTION_DOWN) downTime = SystemClock.uptimeMillis();
        MotionEvent.PointerProperties[] properties = new MotionEvent.PointerProperties[ids.length];
        MotionEvent.PointerCoords[] points = new MotionEvent.PointerCoords[ids.length];
        for (int i = 0; i < ids.length; ++i) {
            properties[i] = new MotionEvent.PointerProperties(); properties[i].id = ids[i]; properties[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
            points[i] = new MotionEvent.PointerCoords(); points[i].x = xs[i]; points[i].y = ys[i]; points[i].pressure = 1;
        }
        MotionEvent event = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), action, ids.length, properties, points, 0, 0, 1, 1, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0);
        assertTrue(UiChecks.automation().injectInputEvent(event, true)); event.recycle();
    }

    private void finger(int action, float px, float py) { touch(action, new int[] {7}, new float[] {px}, new float[] {py}); }

    private void clickRenderedTarget() throws Exception {
        Bitmap screenshot = UiChecks.automation().takeScreenshot(); assertNotNull(screenshot);
        int width = screenshot.getWidth(), height = screenshot.getHeight();
        int[] pixels = new int[width * height]; screenshot.getPixels(pixels, 0, width, 0, 0, width, height); screenshot.recycle();
        int left = width, right = -1, top = height, bottom = -1;
        for (int py = 0; py < height; ++py) for (int px = 0; px < width; ++px) {
            int color = pixels[py * width + px];
            if (Math.abs(((color >> 16) & 255) - 70) + Math.abs(((color >> 8) & 255) - 182) + Math.abs((color & 255) - 161) <= 3) {
                left = Math.min(left, px); right = Math.max(right, px); top = Math.min(top, py); bottom = Math.max(bottom, py);
            }
        }
        assertTrue("The game’s rendered input target is visible", right - left > width / 40 && bottom - top > height / 40);
        float px = (left + right) / 2f, py = (top + bottom) / 2f;
        int before = count("CLICK ");
        finger(MotionEvent.ACTION_DOWN, px, py); finger(MotionEvent.ACTION_UP, px, py);
        waitCount("CLICK ", before + 1);
        String last = ""; for (String line : log().split("\\r?\\n")) if (line.startsWith("CLICK ")) last = line;
        String[] coordinates = last.split(" ");
        assertEquals("Off-center touch x follows the rendered game", 176, Integer.parseInt(coordinates[1]), 6);
        assertEquals("Off-center touch y follows the rendered game", 202, Integer.parseInt(coordinates[2]), 6);
    }

    private void mouse(int action, int buttons, float scroll) {
        MotionEvent.PointerProperties property = new MotionEvent.PointerProperties(); property.id = 0; property.toolType = MotionEvent.TOOL_TYPE_MOUSE;
        MotionEvent.PointerCoords point = new MotionEvent.PointerCoords(); point.x = x; point.y = y; point.pressure = 1;
        point.setAxisValue(MotionEvent.AXIS_VSCROLL, scroll);
        long now = SystemClock.uptimeMillis();
        MotionEvent event = MotionEvent.obtain(now, now, action, 1, new MotionEvent.PointerProperties[] {property}, new MotionEvent.PointerCoords[] {point}, 0, buttons, 1, 1, 0, 0, InputDevice.SOURCE_MOUSE, 0);
        assertTrue(UiChecks.automation().injectInputEvent(event, true)); event.recycle();
    }

    @Test public void realEngineReceivesKeyboardMouseTouchAndReleasesKeysForControls() throws Exception {
        Context context = instrumentation.getTargetContext();
        File folder = new File(context.getFilesDir(), "input-ci"); assertTrue(folder.isDirectory() || folder.mkdirs());
        events = new File(folder, "events.txt");
        String source = new String(bytes(instrumentation.getContext().getAssets().open("engine/input-startup.tjs")), StandardCharsets.UTF_8)
                .replace("@@OUTPUT@@", folder.getPath());
        try (FileOutputStream output = new FileOutputStream(new File(folder, "startup.tjs"))) { output.write(source.getBytes(StandardCharsets.UTF_8)); }
        StorageAccessTest.setup();
        String game = StorageAccess.pathForTree(context, DocumentsContract.buildTreeDocumentUri(TestDocumentsProvider.AUTHORITY, "root/Games")) + "/Controls playground";
        assertTrue(StorageAccess.mkdirs(context, game));
        assertTrue(StorageAccess.write(context, game + "/startup.tjs", source.getBytes(StandardCharsets.UTF_8)));
        assertTrue(StorageAccess.write(context, game + "/Kirikiroid2Preference.xml",
            ("<GlobalPreference><Custom key=\"debugwin\" value=\"no\"/>"
            + "<Custom key=\"gpredetect\" value=\"0\"/><Custom key=\"used2d\" value=\"no\"/>"
            + "</GlobalPreference>").getBytes(StandardCharsets.UTF_8)));
        String secondGame = StorageAccess.pathForTree(context, DocumentsContract.buildTreeDocumentUri(TestDocumentsProvider.AUTHORITY, "root/Games")) + "/Second controls playground";
        assertTrue(StorageAccess.mkdirs(context, secondGame));
        assertTrue(StorageAccess.write(context, secondGame + "/startup.tjs", source.getBytes(StandardCharsets.UTF_8)));
        context.getSharedPreferences("game_controls", 0).edit().clear().commit();
        context.startActivity(new Intent(context, LibraryActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK));
        try {
            UiChecks.waitFor("Your library"); UiChecks.addFolder("Controls playground");
            UiChecks.scrollTo("Play Controls playground"); UiChecks.click("Play Controls playground");
            waitLog("READY");
            UiChecks.waitFor("A few easy controls"); UiChecks.screenshot("controls-first-use"); UiChecks.click("Let’s play");
            UiChecks.waitFor("Open game controls");
            UiChecks.click("Open game controls"); UiChecks.scrollTo("Game preferences"); UiChecks.click("Game preferences");
            UiChecks.waitFor("Game preferences");
            List<AccessibilityNodeInfo> preferenceText = UiChecks.automation().getRootInActiveWindow()
                .findAccessibilityNodeInfosByText("-gpredetect=0");
            assertFalse("Applied preferences are visible", preferenceText.isEmpty());
            String report = preferenceText.get(0).getText().toString();
            assertTrue(report.contains("Game preferences: Loaded"));
            String launchedFolder = StorageAccess.pathForTree(context,
                DocumentsContract.buildTreeDocumentUri(TestDocumentsProvider.AUTHORITY, "root/Games")) + "/Controls playground";
            assertTrue(report.contains(launchedFolder + "/Kirikiroid2Preference.xml"));
            assertTrue(report.contains("-debugwin=no") && report.contains("-used2d=no"));
            UiChecks.screenshot("game-preferences-loaded"); UiChecks.click("OK");
            instrumentation.runOnMainSync(() -> {
                View surface = KR2Activity.sInstance.getGLSurfaceView(); int[] origin = new int[2]; surface.getLocationOnScreen(origin);
                x = origin[0] + surface.getWidth() / 2f; y = origin[1] + surface.getHeight() / 2f;
                surface.requestFocus();
            });
            key(KeyEvent.KEYCODE_SHIFT_LEFT, true, KeyEvent.META_SHIFT_ON);
            key(KeyEvent.KEYCODE_W, true, KeyEvent.META_SHIFT_ON);
            waitLog("KD 87 held=1 shift=1"); waitLog("TEXT W");
            key(KeyEvent.KEYCODE_W, false, KeyEvent.META_SHIFT_ON); key(KeyEvent.KEYCODE_SHIFT_LEFT, false, 0);
            waitLog("KU 87 held=0"); waitLog("KU 16 held=0");
            for (int code : new int[] {KeyEvent.KEYCODE_A, KeyEvent.KEYCODE_S, KeyEvent.KEYCODE_D, KeyEvent.KEYCODE_DPAD_LEFT,
                    KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_DPAD_DOWN,
                    KeyEvent.KEYCODE_F5, KeyEvent.KEYCODE_TAB, KeyEvent.KEYCODE_DEL, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_ESCAPE}) {
                key(code, true, 0); waitLog("KD " + GameInput.windowsKey(code) + " held=1");
                key(code, false, 0); waitLog("KU " + GameInput.windowsKey(code) + " held=0");
            }
            finger(MotionEvent.ACTION_DOWN, x, y); finger(MotionEvent.ACTION_UP, x, y);
            waitLog("CLICK "); waitLog("MU 0 ");
            String click = null; for (String line : log().split("\\r?\\n")) if (line.startsWith("CLICK ")) click = line;
            assertNotNull(click); String[] coordinates = click.split(" ");
            assertEquals("Touch x reaches the game’s coordinates", 320, Integer.parseInt(coordinates[1]), 6);
            assertEquals("Touch y reaches the game’s coordinates", 240, Integer.parseInt(coordinates[2]), 6);
            clickRenderedTarget();
            int clicks = count("CLICK ");
            finger(MotionEvent.ACTION_DOWN, x - 40, y); finger(MotionEvent.ACTION_MOVE, x + 80, y + 20);
            SystemClock.sleep(100); finger(MotionEvent.ACTION_CANCEL, x + 80, y + 20); SystemClock.sleep(150);
            assertEquals("Canceled drags do not click", clicks, count("CLICK "));
            int rights = count("MD 1 ");
            finger(MotionEvent.ACTION_DOWN, x, y);
            touch(MotionEvent.ACTION_POINTER_DOWN | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT), new int[] {7, 21}, new float[] {x, x + 50}, new float[] {y, y + 20});
            touch(MotionEvent.ACTION_POINTER_UP | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT), new int[] {7, 21}, new float[] {x, x + 50}, new float[] {y, y + 20});
            finger(MotionEvent.ACTION_UP, x, y);
            waitLog("MU 1 "); assertEquals(rights + 1, count("MD 1 "));
            mouse(MotionEvent.ACTION_HOVER_MOVE, 0, 0); mouse(MotionEvent.ACTION_DOWN, MotionEvent.BUTTON_PRIMARY, 0);
            mouse(MotionEvent.ACTION_UP, 0, 0); mouse(MotionEvent.ACTION_SCROLL, 0, 2);
            waitLog("WHEEL 240 ");
            key(KeyEvent.KEYCODE_W, true, 0); SystemClock.sleep(100);
            int releases = count("KU 87 ");
            UiChecks.click("Open game controls");
            long deadline = SystemClock.uptimeMillis() + 8000;
            while (count("KU 87 ") == releases && SystemClock.uptimeMillis() < deadline) SystemClock.sleep(80);
            assertEquals("Opening Controls releases W", releases + 1, count("KU 87 "));
            UiChecks.screenshot("controls-panel");
            UiChecks.click("Show Enter, Space, Esc and Ctrl"); UiChecks.click("Touchpad mode");
            UiChecks.click("Resume game"); key(KeyEvent.KEYCODE_W, false, 0); SystemClock.sleep(150);
            assertEquals("Canceled hardware release is not delivered twice", releases + 1, count("KU 87 "));
            UiChecks.waitFor("Hold left mouse button"); UiChecks.screenshot("controls-touchpad");
            UiChecks.click("Open game controls"); UiChecks.click("Type text into the game");
            UiChecks.text("你好"); UiChecks.click("Send text");
            waitLog("TEXT 你"); waitLog("TEXT 好");
            UiChecks.click("Open game controls"); UiChecks.click("Show keyboard");
            UiChecks.waitFor("Game key W"); UiChecks.screenshot("controls-keyboard");
            int w = count("KD 87 "); UiChecks.click("Game key W"); SystemClock.sleep(150);
            assertEquals("On-screen W reaches TJS", w + 1, count("KD 87 "));
            UiChecks.click("Hide keys");
            int aDown = count("KD 65 "), aUp = count("KU 65 ");
            key(KeyEvent.KEYCODE_A, true, 0); waitCount("KD 65 ", aDown + 1);
            key(KeyEvent.KEYCODE_HOME, true, 0); key(KeyEvent.KEYCODE_HOME, false, 0);
            long homeDeadline = SystemClock.uptimeMillis() + 10000;
            boolean homeVisible = false;
            while (SystemClock.uptimeMillis() < homeDeadline) {
                AccessibilityNodeInfo root = UiChecks.automation().getRootInActiveWindow();
                if (root != null && root.getPackageName() != null && !context.getPackageName().contentEquals(root.getPackageName())) { homeVisible = true; break; }
                SystemClock.sleep(80);
            }
            assertTrue("Home backgrounds the game before resuming it", homeVisible);
            context.startActivity(new Intent(context, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            UiChecks.waitFor("Open game controls");
            waitCount("KU 65 ", aUp + 1);
            key(KeyEvent.KEYCODE_A, false, 0); SystemClock.sleep(150);
            assertEquals("Background/resume releases A without duplicating its later key-up", aUp + 1, count("KU 65 "));
            // The framework stops the test process after reporting success; do not finish the legacy activity here.
        } finally {
            UiChecks.screenshot("controls-final");
            key(KeyEvent.KEYCODE_HOME, true, 0); key(KeyEvent.KEYCODE_HOME, false, 0);
            SystemClock.sleep(300);
        }
    }
}
