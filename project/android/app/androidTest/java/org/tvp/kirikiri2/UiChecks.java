package org.tvp.kirikiri2;

import android.app.UiAutomation;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Rect;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.File;
import java.io.FileOutputStream;
import java.util.Locale;
import org.xmlpull.v1.XmlSerializer;
import static org.junit.Assert.*;

/** Uses the actual accessibility tree in both the app and the system folder picker. */
final class UiChecks {
    static UiAutomation automation() { return InstrumentationRegistry.getInstrumentation().getUiAutomation(); }

    private static AccessibilityNodeInfo match(AccessibilityNodeInfo root, String name, boolean editable) {
        if (root == null) return null;
        String text = root.getText() == null ? "" : root.getText().toString();
        String description = root.getContentDescription() == null ? "" : root.getContentDescription().toString();
        if (root.isVisibleToUser() && (editable ? root.isEditable() : text.equalsIgnoreCase(name) || description.equalsIgnoreCase(name))) return root;
        for (int i = 0; i < root.getChildCount(); ++i) {
            AccessibilityNodeInfo result = match(root.getChild(i), name, editable);
            if (result != null) return result;
        }
        return null;
    }

    static AccessibilityNodeInfo find(String name) { return match(automation().getRootInActiveWindow(), name, false); }

    static AccessibilityNodeInfo waitFor(String name) {
        long deadline = SystemClock.uptimeMillis() + 15000;
        AccessibilityNodeInfo result;
        while ((result = find(name)) == null && SystemClock.uptimeMillis() < deadline) SystemClock.sleep(100);
        assertNotNull("Visible UI element: " + name, result);
        return result;
    }

    static void click(String name) {
        AccessibilityNodeInfo node = waitFor(name);
        while (node != null && !node.isClickable()) node = node.getParent();
        assertNotNull("Clickable UI element: " + name, node);
        Rect bounds = new Rect(); node.getBoundsInScreen(bounds);
        assertFalse("Visible bounds: " + name, bounds.isEmpty());
        long now = SystemClock.uptimeMillis();
        MotionEvent down = MotionEvent.obtain(now, now, MotionEvent.ACTION_DOWN, bounds.centerX(), bounds.centerY(), 0);
        MotionEvent up = MotionEvent.obtain(now, now + 32, MotionEvent.ACTION_UP, bounds.centerX(), bounds.centerY(), 0);
        down.setSource(InputDevice.SOURCE_TOUCHSCREEN); up.setSource(InputDevice.SOURCE_TOUCHSCREEN);
        try {
            assertTrue("Touch down: " + name, automation().injectInputEvent(down, true));
            assertTrue("Touch up: " + name, automation().injectInputEvent(up, true));
        } finally { down.recycle(); up.recycle(); }
        SystemClock.sleep(150);
    }

    static void text(String value) {
        long deadline = SystemClock.uptimeMillis() + 10000;
        AccessibilityNodeInfo edit;
        while ((edit = match(automation().getRootInActiveWindow(), "", true)) == null && SystemClock.uptimeMillis() < deadline) SystemClock.sleep(100);
        assertNotNull("An editable field is visible", edit);
        Bundle arguments = new Bundle(); arguments.putCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE, value);
        assertTrue(edit.performAction(AccessibilityNodeInfo.ACTION_SET_TEXT, arguments));
    }

    static void screenshot(String name) throws Exception {
        Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        File folder = new File(context.getFilesDir(), "ui-evidence"); assertTrue(folder.isDirectory() || folder.mkdirs());
        Bitmap image = automation().takeScreenshot(); assertNotNull(image);
        try (FileOutputStream output = new FileOutputStream(new File(folder, name + ".png"))) {
            assertTrue(image.compress(Bitmap.CompressFormat.PNG, 100, output));
        }
        image.recycle();
        try (FileOutputStream output = new FileOutputStream(new File(folder, name + ".xml"))) {
            XmlSerializer xml = android.util.Xml.newSerializer(); xml.setOutput(output, "UTF-8");
            xml.startDocument("UTF-8", true); writeNode(xml, automation().getRootInActiveWindow()); xml.endDocument();
        }
    }

    private static void writeNode(XmlSerializer xml, AccessibilityNodeInfo node) throws Exception {
        if (node == null) return;
        xml.startTag(null, "node");
        xml.attribute(null, "text", node.getText() == null ? "" : node.getText().toString());
        xml.attribute(null, "description", node.getContentDescription() == null ? "" : node.getContentDescription().toString());
        xml.attribute(null, "class", String.valueOf(node.getClassName()));
        xml.attribute(null, "id", String.valueOf(node.getViewIdResourceName()));
        xml.attribute(null, "clickable", String.valueOf(node.isClickable()));
        xml.attribute(null, "visible", String.valueOf(node.isVisibleToUser()));
        Rect bounds = new Rect(); node.getBoundsInScreen(bounds); xml.attribute(null, "bounds", bounds.toShortString());
        for (int i = 0; i < node.getChildCount(); ++i) writeNode(xml, node.getChild(i));
        xml.endTag(null, "node");
    }

    static void back() {
        InstrumentationRegistry.getInstrumentation().sendKeyDownUpSync(android.view.KeyEvent.KEYCODE_BACK);
        SystemClock.sleep(150);
    }

    static void addFolder(String name) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            scrollTo("+  Add game folder"); click("+  Add game folder");
            waitFor("Use this folder"); settlePicker();
            if (find("Show roots") != null || find("Kirikiri test games") == null) click("Show roots");
            click("Kirikiri test games"); settlePicker();
            try { scrollTo(name); }
            catch (AssertionError error) {
                // On first boot, DocumentsUI can deliver the preceding internal
                // storage listing after changing the root title. Reopen only
                // for that observed state; do not retry a missing fixture.
                int oldRows = 0;
                for (String item : new String[] {"Alarms", "Android", "DCIM", "Download", "Movies", "Music", "Pictures", "Podcasts", "Recordings", "Ringtones"})
                    if (find(item) != null) ++oldRows;
                if (attempt != 0 || find("Kirikiri test games") == null || oldRows < 2) throw error;
                android.util.Log.w("UiChecks", "Reopening picker after stale internal-storage listing");
                back(); waitFor("Your library"); continue;
            }
            click(name); click("Use this folder"); click("Allow"); waitFor(name);
            return;
        }
        fail("Could not select game folder: " + name);
    }

    private static void settlePicker() {
        try { automation().waitForIdle(750, 10000); }
        catch (java.util.concurrent.TimeoutException busySystem) { /* Content assertions below still apply. */ }
    }

    static void scrollTo(String name) {
        for (int attempt = 0; attempt < 12 && find(name) == null; ++attempt) {
            AccessibilityNodeInfo root = automation().getRootInActiveWindow();
            scroll(root); SystemClock.sleep(150);
        }
        waitFor(name);
    }

    private static boolean scroll(AccessibilityNodeInfo node) {
        if (node == null) return false;
        if (node.isScrollable() && node.performAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD)) return true;
        for (int i = 0; i < node.getChildCount(); ++i) if (scroll(node.getChild(i))) return true;
        return false;
    }
}
