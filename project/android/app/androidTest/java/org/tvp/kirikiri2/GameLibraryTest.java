package org.tvp.kirikiri2;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.SharedPreferences;
import android.net.Uri;
import android.provider.DocumentsContract;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;
import java.util.List;
import java.io.IOException;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public class GameLibraryTest {
    private static Context context;
    private static Uri tree;
    private static String mount;

    @BeforeClass public static void setup() throws Exception {
        StorageAccessTest.setup();
        Context base = InstrumentationRegistry.getInstrumentation().getTargetContext();
        context = new ContextWrapper(base) {
            @Override public SharedPreferences getSharedPreferences(String name, int mode) {
                return super.getSharedPreferences("test_" + name, mode);
            }
        };
        tree = DocumentsContract.buildTreeDocumentUri(TestDocumentsProvider.AUTHORITY, "root/Games");
        mount = StorageAccess.pathForTree(context, tree);
    }

    private void file(String path) { assertTrue(StorageAccess.write(context, mount + "/" + path, new byte[] {0})); }

    @Test public void discoveryChoosesStartupOrDataAndAsksForAmbiguousArchives() throws Exception {
        assertTrue(StorageAccess.mkdirs(context, mount + "/LibraryData"));
        assertTrue(StorageAccess.mkdirs(context, mount + "/LibrarySource"));
        assertTrue(StorageAccess.mkdirs(context, mount + "/LibraryChoice"));
        assertTrue(StorageAccess.mkdirs(context, mount + "/LibraryZip"));
        file("LibraryData/patch.xp3"); file("LibraryData/data.xp3");
        file("LibrarySource/startup.tjs"); file("LibrarySource/data.xp3");
        file("LibraryChoice/story.xp3"); file("LibraryChoice/patch.xp3");
        file("LibraryZip/game.zip");
        List<GameLibrary.Folder> found = GameLibrary.discover(context, tree);
        int checked = 0;
        for (GameLibrary.Folder folder : found) {
            if (folder.entry.folder.equals("LibraryData")) {
                assertEquals("data.xp3", folder.entry.launchFile);
                assertEquals(mount + "/LibraryData/data.xp3", folder.entry.path(context)); checked++;
            } else if (folder.entry.folder.equals("LibrarySource")) {
                assertEquals("", folder.entry.launchFile); checked++;
            } else if (folder.entry.folder.equals("LibraryChoice")) {
                assertTrue(folder.needsChoice()); assertEquals(2, folder.candidates.size()); checked++;
            }
            assertNotEquals("An unextracted ZIP is not a playable game", "LibraryZip", folder.entry.folder);
        }
        assertEquals(3, checked);
    }

    @Test public void libraryPersistsFavoritesLaunchChoiceAndDoesNotDeleteGames() throws Exception {
        context.getSharedPreferences("game_library", 0).edit().clear().commit();
        assertTrue(StorageAccess.mkdirs(context, mount + "/LibraryKeep"));
        file("LibraryKeep/data.xp3"); file("LibraryKeep/savedata.dat");
        GameLibrary library = new GameLibrary(context);
        GameLibrary.Entry entry = new GameLibrary.Entry(tree.toString(), "LibraryKeep", "A Story", "data.xp3");
        library.put(entry); entry.favorite = true; entry.lastPlayed = 123456; entry.name = "Renamed Story"; library.put(entry);
        List<GameLibrary.Entry> restored = new GameLibrary(context).load();
        assertEquals(1, restored.size()); assertEquals("Renamed Story", restored.get(0).name);
        assertTrue(restored.get(0).favorite); assertEquals(123456, restored.get(0).lastPlayed);
        assertEquals("data.xp3", restored.get(0).launchFile);
        assertEquals(1, GameLibrary.filter(restored, " STORY ", 2).size());
        assertTrue(GameLibrary.filter(restored, "absent", 0).isEmpty());
        library.remove(entry); assertTrue(library.load().isEmpty());
        assertNotNull(StorageAccess.stat(context, mount + "/LibraryKeep/data.xp3"));
        assertNotNull(StorageAccess.stat(context, mount + "/LibraryKeep/savedata.dat"));
    }

    @Test public void missingPermissionNeverFallsBackToAnotherGrantedTree() throws Exception {
        Uri missing = DocumentsContract.buildTreeDocumentUri(TestDocumentsProvider.AUTHORITY, "root/NotGranted");
        GameLibrary.Entry entry = new GameLibrary.Entry(missing.toString(), "", "Missing", "data.xp3");
        try { entry.path(context); fail("Missing grants must be reported"); }
        catch (IOException expected) { assertTrue(expected.getMessage().contains("permission")); }
    }
}
