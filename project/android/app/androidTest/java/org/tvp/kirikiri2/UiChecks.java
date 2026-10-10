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
        click(waitFor(name), name);
    }

    private static void click(AccessibilityNodeInfo node, String name) {
        while (node != null && !node.isClickable()) node = node.getParent();
        assertNotNull("Clickable UI element: " + name, node);
        // Activate the actual view, without racing the moving coordinates of
        // Android 16's folder-picker drawer. NativeControlsTest separately
        // injects and verifies real touch, mouse and keyboard input events.
        assertTrue("Click UI element: " + name, node.performAction(AccessibilityNodeInfo.ACTION_CLICK));
        SystemClock.sleep(150);
    }

    private static AccessibilityNodeInfo listItem(AccessibilityNodeInfo root, String name) {
        if (root == null) return null;
        if ("android.widget.ListView".contentEquals(root.getClassName())) return match(root, name, false);
        for (int i = 0; i < root.getChildCount(); ++i) {
            AccessibilityNodeInfo result = listItem(root.getChild(i), name);
            if (result != null) return result;
        }
        return null;
    }

    private static void clickPickerRoot(String name) {
        // DocumentsUI can report a clickable ListView row whose accessibility
        // ACTION_CLICK returns false. Settle the drawer before acquiring the
        // row, since the active accessibility root can change during settling.
        settleUi();
        long deadline = SystemClock.uptimeMillis() + 15000;
        AccessibilityNodeInfo node;
        while ((node = listItem(automation().getRootInActiveWindow(), name)) == null
                && SystemClock.uptimeMillis() < deadline) SystemClock.sleep(100);
        assertNotNull("Folder-picker root list item: " + name, node);
        tap(node, name);
    }

    static void tap(String name) { tap(waitFor(name), name); }

    private static void tap(AccessibilityNodeInfo node, String name) {
        Rect bounds = new Rect(); node.getBoundsInScreen(bounds);
        assertFalse("Visible tap bounds: " + name, bounds.isEmpty());
        long downTime = SystemClock.uptimeMillis();
        for (int action : new int[] {MotionEvent.ACTION_DOWN, MotionEvent.ACTION_UP}) {
            MotionEvent event = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), action,
                    bounds.exactCenterX(), bounds.exactCenterY(), 0);
            event.setSource(InputDevice.SOURCE_TOUCHSCREEN);
            try { assertTrue("Tap UI element: " + name, automation().injectInputEvent(event, true)); }
            finally { event.recycle(); }
            SystemClock.sleep(100);
        }
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
        xml.attribute(null, "scrollable", String.valueOf(node.isScrollable()));
        Rect bounds = new Rect(); node.getBoundsInScreen(bounds); xml.attribute(null, "bounds", bounds.toShortString());
        for (int i = 0; i < node.getChildCount(); ++i) writeNode(xml, node.getChild(i));
        xml.endTag(null, "node");
    }

    static void back() {
        InstrumentationRegistry.getInstrumentation().sendKeyDownUpSync(android.view.KeyEvent.KEYCODE_BACK);
        SystemClock.sleep(150);
    }

    static void addFolder(String name) {
        // Grant the games root through the real system picker, then use the
        // app's chooser. DocumentsUI loads child listings asynchronously.
        scrollTo("+  Add game folder"); click("+  Add game folder");
        waitFor("Use this folder"); settleUi();
        if (find("Show roots") != null) click("Show roots");
        // The selected root name also appears in the toolbar; select the
        // drawer's list item specifically when reusing a previous grant.
        clickPickerRoot("Kirikiri test games"); settleUi();
        long deadline = SystemClock.uptimeMillis() + 15000;
        boolean ready = false;
        do {
            AccessibilityNodeInfo confirm = find("Use this folder");
            ready = confirm != null && confirm.isEnabled() && find("Show roots") != null
                && find("Kirikiri test games") != null;
            if (ready) break;
            SystemClock.sleep(150);
        } while (SystemClock.uptimeMillis() < deadline);
        assertTrue("Selected fixture provider in Android picker", ready);
        click("Use this folder");
        // The title can already be visible behind a pending discovery. Only a
        // completed library card proves that selecting the game has finished.
        deadline = SystemClock.uptimeMillis() + 15000;
        while (find("Options for " + name) == null && SystemClock.uptimeMillis() < deadline) {
            // Android omits the permission dialog when this tree was already
            // granted by an earlier library operation.
            if (find("Allow") != null) click("Allow");
            else if (find("Choose a game folder") != null) { scrollTo(name); click(name); }
            else if (find("Your library") != null) scroll(automation().getRootInActiveWindow());
            SystemClock.sleep(200);
        }
        waitFor("Options for " + name);
    }

    private static void settleUi() {
        try { automation().waitForIdle(750, 10000); }
        catch (java.util.concurrent.TimeoutException busySystem) { /* Content assertions below still apply. */ }
    }

    static void scrollTo(String name) {
        settleUi();
        // A new dialog can expose its tree after the first lookup, and a
        // retained scroll position can place the target above the viewport.
        for (int direction : new int[] {AccessibilityNodeInfo.ACTION_SCROLL_FORWARD,
                AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD}) {
            int unavailable = 0;
            for (int attempt = 0; attempt < 12; ++attempt) {
                if (find(name) != null) return;
                // A newly opened dialog can expose its labels before its
                // ScrollView supports scrolling. Reacquire after layout.
                if (!scroll(automation().getRootInActiveWindow(), direction)) {
                    if (++unavailable >= 3) break;
                    SystemClock.sleep(250);
                } else unavailable = 0;
                settleUi();
            }
        }
        waitFor(name);
    }

    private static boolean scroll(AccessibilityNodeInfo node) {
        return scroll(node, AccessibilityNodeInfo.ACTION_SCROLL_FORWARD);
    }

    private static boolean scroll(AccessibilityNodeInfo node, int direction) {
        if (node == null) return false;
        if (node.isVisibleToUser() && node.isScrollable()) {
            if (node.performAction(direction)) return true;
            // Some Android 16 ScrollViews reject the accessibility action;
            // exercise the same bounded drag a user would make in the panel.
            Rect bounds = new Rect(); node.getBoundsInScreen(bounds);
            if (bounds.width() > 0 && bounds.height() > 80) {
                float x = bounds.exactCenterX();
                float start = bounds.top + bounds.height() * .75f;
                float end = bounds.top + bounds.height() * .25f;
                if (direction == AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD) {
                    float swap = start; start = end; end = swap;
                }
                long down = SystemClock.uptimeMillis();
                for (int step = 0; step <= 12; ++step) {
                    int action = step == 0 ? MotionEvent.ACTION_DOWN
                        : step == 12 ? MotionEvent.ACTION_UP : MotionEvent.ACTION_MOVE;
                    MotionEvent event = MotionEvent.obtain(down, SystemClock.uptimeMillis(),
                        action, x, start + (end - start) * step / 12f, 0);
                    event.setSource(InputDevice.SOURCE_TOUCHSCREEN);
                    boolean injected = automation().injectInputEvent(event, true);
                    event.recycle();
                    assertTrue("Scroll touch injected", injected);
                    SystemClock.sleep(25);
                }
                return true;
            }
        }
        for (int i = 0; i < node.getChildCount(); ++i) if (scroll(node.getChild(i), direction)) return true;
        return false;
    }
}
