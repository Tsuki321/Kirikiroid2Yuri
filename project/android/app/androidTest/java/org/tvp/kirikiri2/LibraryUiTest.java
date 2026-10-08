package org.tvp.kirikiri2;

import android.app.UiAutomation;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.SystemClock;
import android.provider.DocumentsContract;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import com.yuri.kirikiri2.LibraryActivity;
import org.junit.Test;
import org.junit.runner.RunWith;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public class LibraryUiTest {
    @Test public void addFolderThroughSystemPickerSearchRenameFavoriteAndRemove() throws Exception {
        StorageAccessTest.setup();
        Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        Uri tree = DocumentsContract.buildTreeDocumentUri(TestDocumentsProvider.AUTHORITY, "root/Games");
        String path = StorageAccess.pathForTree(context, tree) + "/Library UI Story";
        assertTrue(StorageAccess.mkdirs(context, path));
        assertTrue(StorageAccess.write(context, path + "/startup.tjs", "// UI fixture".getBytes("UTF-8")));
        assertTrue(StorageAccess.write(context, path + "/save.dat", new byte[] {1, 2, 3}));
        context.startActivity(new Intent(context, LibraryActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK));
        try {
            UiChecks.waitFor("Your library"); UiChecks.screenshot("library-empty");
            UiChecks.addFolder("Library UI Story");
            UiChecks.waitFor("Library UI Story");
            UiChecks.click("Options for Library UI Story"); UiChecks.click("Add to favorites");
            UiChecks.click("Options for Library UI Story"); UiChecks.click("Rename");
            UiChecks.text("A new story"); UiChecks.click("Save");
            UiChecks.waitFor("Options for A new story");
            UiChecks.text("missing story"); UiChecks.waitFor("No matching games. Try another search or filter.");
            UiChecks.text("new"); UiChecks.waitFor("Options for A new story");
            UiChecks.text("");
            UiChecks.automation().setRotation(UiAutomation.ROTATION_FREEZE_0); SystemClock.sleep(500);
            UiChecks.screenshot("library-portrait");
            UiChecks.automation().setRotation(UiAutomation.ROTATION_FREEZE_90); SystemClock.sleep(500);
            UiChecks.scrollTo("Options for A new story"); UiChecks.screenshot("library-landscape");
            UiChecks.click("Options for A new story"); UiChecks.click("Remove from library"); UiChecks.click("Remove");
            UiChecks.waitFor("No games added yet. Use Add game folder to get started.");
            assertNotNull("Removing a shortcut preserves the game", StorageAccess.stat(context, path + "/startup.tjs"));
            assertNotNull("Removing a shortcut preserves saves", StorageAccess.stat(context, path + "/save.dat"));
        } finally {
            UiChecks.screenshot("library-final");
            UiChecks.automation().setRotation(UiAutomation.ROTATION_UNFREEZE);
        }
    }
}
