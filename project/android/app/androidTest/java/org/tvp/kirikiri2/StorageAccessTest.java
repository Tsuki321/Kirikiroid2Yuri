package org.tvp.kirikiri2;

import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import org.junit.BeforeClass;
import org.junit.FixMethodOrder;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.MethodSorters;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
@FixMethodOrder(MethodSorters.NAME_ASCENDING)
public class StorageAccessTest {
    private static Context context;
    private static String documents;
    private static byte[] readBytes(InputStream input) throws Exception {
        try (InputStream stream = input; ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096]; int count;
            while ((count = stream.read(buffer)) != -1) output.write(buffer, 0, count);
            return output.toByteArray();
        }
    }
    private static String read(InputStream input) throws Exception {
        return new String(readBytes(input), StandardCharsets.UTF_8);
    }
    private static void write(String path, String text) { assertTrue(path, StorageAccess.write(context, path, text.getBytes(StandardCharsets.UTF_8))); }
    @BeforeClass public static void setup() throws Exception {
        context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        Intent grant = new Intent().setClassName(
            InstrumentationRegistry.getInstrumentation().getContext().getPackageName(), GrantDocumentActivity.class.getName());
        context.startActivity(grant.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        Uri tree = android.provider.DocumentsContract.buildTreeDocumentUri(TestDocumentsProvider.AUTHORITY, "root/Games");
        boolean granted = false;
        long deadline = android.os.SystemClock.uptimeMillis() + 10000;
        while (android.os.SystemClock.uptimeMillis() < deadline) {
            try {
                context.getContentResolver().takePersistableUriPermission(tree,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                granted = true; break;
            } catch (SecurityException pending) { Thread.sleep(50); }
        }
        assertTrue("Provider activity must grant a persistable tree", granted);
        String[] roots = StorageAccess.roots(context);
        assertTrue(roots.length > 0);
        for (String root : roots) if (root.startsWith("/documents/Games-")) documents = root;
        assertNotNull(documents);
    }
    @Test public void aExistingDirectoriesAndFailedParents() throws Exception {
        File parent = new File(context.getCacheDir(), "storage-tests");
        assertTrue(StorageAccess.mkdirs(context, parent.getPath()));
        assertTrue(StorageAccess.mkdirs(context, parent.getPath()));
        write(new File(parent, "file").getPath(), "sentinel");
        assertFalse(StorageAccess.mkdirs(context, new File(parent, "file/child").getPath()));
        assertFalse(StorageAccess.write(context, "/proc/krkr-ci-denied", new byte[] {1}));
        assertEquals("sentinel", read(new FileInputStream(new File(parent, "file"))));
    }
    @Test public void bAtomicLocalWritesAndReplacement() throws Exception {
        File file = new File(context.getCacheDir(), "save-test");
        write(file.getPath(), "a long original save");
        write(file.getPath(), "new");
        assertEquals("new", read(new FileInputStream(file)));
        assertFalse(new File(file.getPath() + ".bak").exists());
    }
    @Test public void cDocumentReadWriteListAndCaseFolding() throws Exception {
        String folder = documents + "/Scenario";
        assertTrue(StorageAccess.mkdirs(context, folder));
        assertTrue(StorageAccess.mkdirs(context, folder));
        String path = folder + "/日本語.txt";
        write(path, "unicode fixture");
        long[] info = StorageAccess.stat(context, path);
        assertNotNull(info); assertEquals(15, info[1]);
        assertEquals(4, StorageAccess.list(context, folder).length);
        int fd = StorageAccess.open(context, documents + "/scenario/日本語.txt", 0);
        assertTrue(fd >= 0);
        assertEquals("unicode fixture", read(new ParcelFileDescriptor.AutoCloseInputStream(ParcelFileDescriptor.adoptFd(fd))));
        write(path, "x");
        assertEquals(1, StorageAccess.stat(context, path)[1]);
    }
    private static int documentQueryCount() {
        Uri tree = android.provider.DocumentsContract.buildTreeDocumentUri(TestDocumentsProvider.AUTHORITY, "root/Games");
        Uri counter = android.provider.DocumentsContract.buildDocumentUriUsingTree(tree, "root/Games/query-count");
        try (android.database.Cursor cursor = context.getContentResolver().query(counter, new String[] { "queries" }, null, null, null)) {
            assertNotNull(cursor);
            assertTrue(cursor.moveToFirst());
            return cursor.getInt(0);
        }
    }
    @Test public void cDocumentLookupBatchesNamesWithoutCachingMisses() throws Exception {
        String folder = documents + "/BatchLookup";
        assertTrue(StorageAccess.mkdirs(context, folder));
        for (int i = 0; i < 24; ++i) write(folder + "/neighbor-" + i + ".txt", "neighbor");
        write(folder + "/Scene.TJS", "case variant");
        int before = documentQueryCount();
        assertNull(StorageAccess.stat(context, folder + "/newscene.tjs"));
        assertTrue("Missing leaf must use bounded directory queries", documentQueryCount() - before <= 5);
        write(folder + "/NewScene.TJS", "created after miss");
        assertEquals(18, StorageAccess.stat(context, folder + "/newscene.tjs")[1]);
        // Create a distinct exact-case file through the provider, bypassing the
        // engine's case-folding create semantics. Exact spelling wins even if
        // a differently cased entry appeared earlier in the directory cursor.
        android.provider.DocumentsContract.createDocument(context.getContentResolver(),
            StorageAccess.resolve(context, folder, false, true).getUri(), "application/octet-stream", "scene.tjs");
        before = documentQueryCount();
        assertEquals(0, StorageAccess.stat(context, folder + "/scene.tjs")[1]);
        assertTrue("Exact lookup must not query each sibling", documentQueryCount() - before <= 5);
        int fd = StorageAccess.open(context, folder + "/SCENE.TJS", 0);
        assertTrue(fd >= 0);
        try (ParcelFileDescriptor descriptor = ParcelFileDescriptor.adoptFd(fd)) { /* ensure descriptor is closed */ }
        before = documentQueryCount();
        String[] entries = StorageAccess.list(context, folder);
        assertNotNull(entries);
        assertEquals(27 * 4, entries.length);
        assertTrue("Listing metadata must not query each child", documentQueryCount() - before <= 5);
        assertTrue(StorageAccess.delete(context, folder + "/NewScene.TJS"));
        assertNull(StorageAccess.stat(context, folder + "/newscene.tjs"));
        assertTrue(StorageAccess.delete(context, folder));
    }
    @Test public void dDocumentRenameDeleteAndGrantBoundaries() {
        String before = documents + "/before.txt", after = documents + "/after.txt";
        write(before, "fixture");
        assertTrue(StorageAccess.rename(context, before, after));
        assertNull(StorageAccess.stat(context, before));
        assertNotNull(StorageAccess.stat(context, after));
        assertTrue(StorageAccess.delete(context, after));
        assertNull(StorageAccess.stat(context, after));
        assertEquals(-1, StorageAccess.open(context, documents + "/../not-granted.txt", 1));
        assertNull(StorageAccess.list(context, "/documents/not-granted"));
    }
    @Test public void dRemoveDocumentChecksTypeAndDirectoryContents() {
        String folder = documents + "/remove-directory", file = folder + "/日本語.txt";
        write(file, "preserve until explicitly removed");
        assertFalse(StorageAccess.removeDocument(context, folder, true));
        assertFalse(StorageAccess.removeDocument(context, folder, false));
        assertFalse(StorageAccess.removeDocument(context, file, true));
        assertNotNull(StorageAccess.stat(context, file));
        assertTrue(StorageAccess.removeDocument(context, file, false));
        assertFalse(StorageAccess.removeDocument(context, file, false));
        assertTrue(StorageAccess.removeDocument(context, folder, true));
        assertNull(StorageAccess.stat(context, folder));
        assertFalse(StorageAccess.removeDocument(context, "/documents/not-granted/file.txt", false));
    }
    @Test public void eRasterizerProducesArgbPixels() {
        float[] rectangle = {0,10,10,1,40,10,1,40,30,1,10,30,3};
        int[] patch = LayerPainter.render(rectangle, new float[] {1,0,0,1,0,0},
            new int[] {0xff336699,0xff336699}, new float[] {-1,0,0,0,0,0,1,0,0,0,10,1},
            new float[] {0,0,320,240}, "",12,0,null,true,null,0,0);
        assertTrue(patch[2] > 0 && patch[3] > 0);
        int index = 4 + (15 - patch[1]) * patch[2] + (15 - patch[0]);
        assertEquals(0xff336699, patch[index]);
    }
    @Test public void fStartupPermissionScopeMatchesStorageBoundaries() {
        assertFalse(KR2Activity.needsLegacyStoragePermissionForStartup(context,
                new File(context.getFilesDir(), "game").getPath()));
        assertFalse(KR2Activity.needsLegacyStoragePermissionForStartup(context,
                new File(context.getCacheDir(), "game").getPath()));
        assertFalse(KR2Activity.needsLegacyStoragePermissionForStartup(context, documents + "/game"));
        assertFalse(KR2Activity.needsLegacyStoragePermissionForStartup(context, "file://." + documents + "/game"));
        assertFalse(KR2Activity.needsLegacyStoragePermissionForStartup(context,
                new File(context.getFilesDir(), "game").toURI().toString()));
        for (File directory : context.getExternalFilesDirs(null))
            if (directory != null) assertFalse(KR2Activity.needsLegacyStoragePermissionForStartup(context,
                    new File(directory, "game").getPath()));
        assertTrue(KR2Activity.needsLegacyStoragePermissionForStartup(context, null));
        assertTrue(KR2Activity.needsLegacyStoragePermissionForStartup(context, "/storage/emulated/0/game"));
        assertTrue(KR2Activity.needsLegacyStoragePermissionForStartup(context, "/documents-sibling/game"));
        assertTrue(KR2Activity.needsLegacyStoragePermissionForStartup(context,
                "file://remote" + context.getFilesDir().getPath() + "/game"));
        assertTrue(KR2Activity.needsLegacyStoragePermissionForStartup(context,
                context.getApplicationInfo().dataDir + "-sibling/game"));
        assertTrue(KR2Activity.needsLegacyStoragePermissionForStartup(context,
                new File(context.getFilesDir(), "../../outside").getPath()));
        assertTrue(KR2Activity.needsLegacyStoragePermissionForStartup(context,
                documents + "/../../storage/emulated/0/game"));
    }
    @Test public void zPrepareEngineFixturesForProcessRestart() throws Exception {
        Context tests = InstrumentationRegistry.getInstrumentation().getContext();
        String script = read(tests.getAssets().open("engine/startup.tjs"));
        String movieScript = read(tests.getAssets().open("engine/movie-startup.tjs"));
        String presentationScript = read(tests.getAssets().open("engine/presentation-startup.tjs"));
        String archiveScript = read(tests.getAssets().open("engine/archive-startup.tjs"));
        String transitionScript = read(tests.getAssets().open("engine/transition-startup.tjs"));
        String audioScript = read(tests.getAssets().open("engine/audio-startup.tjs"));
        String dialogScript = read(tests.getAssets().open("engine/dialog-startup.tjs"));
        String preferenceScript = read(tests.getAssets().open("engine/preference-startup.tjs"));
        String program = read(tests.getAssets().open("engine/compiled-source.tjs"));
        File base = new File(context.getFilesDir(), "engine-ci");
        assertTrue(StorageAccess.mkdirs(context, base.getPath()));
        String globalPreferences = new File(context.getFilesDir(), ".preference").getPath();
        assertTrue(StorageAccess.mkdirs(context, globalPreferences));
        write(globalPreferences + "/GlobalPreference.xml", "<GlobalPreference>"
            + "<Custom key=\"ci-global-only\" value=\"inherited\"/>"
            + "<Custom key=\"ci-preference\" value=\"global fallback\"/>"
            + "<Custom key=\"debugwin\" value=\"no\"/></GlobalPreference>");
        StringBuilder manifest = new StringBuilder();
        String[] names = {"local", "documents", "movie-local", "movie-documents", "movie-opengl-local", "movie-opengl-documents", "archive-local", "archive-documents",
                "archive-launch-local", "archive-launch-documents",
                "transitions", "transitions-documents", "transitions-opengl", "transitions-opengl-documents",
                "audio-local", "audio-documents", "dialogs-local", "dialogs-documents",
                "presentation-local", "presentation-documents",
                "preferences-missing-local", "preferences-invalid-local", "preferences-invalid-documents", "preferences-root-local"};
        for (String name : names) {
            String storage = name.contains("documents") ? documents + "/" + name : new File(base, name).getPath();
            String output = new File(base, name + "-result").getPath();
            assertTrue(StorageAccess.mkdirs(context, storage));
            assertTrue(StorageAccess.mkdirs(context, output));
            if (!name.startsWith("preferences-missing")) write(storage + "/Kirikiroid2Preference.xml", "<?xml version=\"1.0\"?>\n<GlobalPreference>"
                + "<Item key=\"renderer\" value=\"" + (name.contains("opengl") ? "opengl" : "software") + "\"/>"
                + "<Custom key=\"ci-preference\" value=\"saved value=1 &amp; 2\"/>"
                + "<Custom key=\"ci-launch\" value=\"overridden preference\"/>"
                + "<Custom key=\"debugwin\" value=\"no\"/><Custom key=\"gpredetect\" value=\"0\"/>"
                + "<Custom key=\"used2d\" value=\"no\"/>"
                + "<Custom key=\"ci-archive-output\" value=\"" + output + "\"/>"
                + "</GlobalPreference>\n");
            if (name.startsWith("preferences-invalid")) write(storage + "/Kirikiroid2Preference.xml",
                "<GlobalPreference><Custom key=\"ci-preference\" value=\"must not leak\"/>");
            if (name.startsWith("preferences-root")) write(storage + "/Kirikiroid2Preference.xml",
                "<Preferences><Custom key=\"ci-preference\" value=\"must not leak\"/></Preferences>");
            write(storage + "/startup.tjs", (name.startsWith("movie") ? movieScript : name.startsWith("archive") ? archiveScript
                : name.startsWith("transitions") ? transitionScript : name.startsWith("audio-") ? audioScript
                : name.startsWith("presentation-") ? presentationScript
                : name.startsWith("dialogs-") ? dialogScript : name.startsWith("preferences-") ? preferenceScript : script)
                .replace("@@STORAGE@@", storage).replace("@@OUTPUT@@", output));
            if (name.startsWith("presentation-")) {
                assertTrue(StorageAccess.delete(context, output + "/presentation-stage.txt"));
                assertTrue(StorageAccess.delete(context, output + "/presentation-ack.txt"));
                assertTrue(StorageAccess.delete(context, output + "/result.txt"));
            }
            if (name.equals("local") || name.equals("documents")) {
                write(storage + "/engine-body.tjs", script.replace("@@STORAGE@@", storage).replace("@@OUTPUT@@", output));
                write(storage + "/compiled-startup-source.tjs",
                    "global.ciColdCompiledStartup = 1;\nScripts.execStorage('engine-body.tjs');\n");
            }
            if (name.startsWith("movie")) {
                for (String asset : new String[] {"test.avi", "test-av.avi", "test-delayed.mp4", "movie-assets.xp3"}) {
                    assertTrue(StorageAccess.write(context, storage + "/" + asset,
                            readBytes(tests.getAssets().open("engine/" + asset))));
                }
            }
            try (InputStream input = tests.getAssets().open("engine/tone.wav"); ByteArrayOutputStream tone = new ByteArrayOutputStream()) {
                byte[] buffer = new byte[8192]; int size;
                while ((size = input.read(buffer)) != -1) tone.write(buffer, 0, size);
                assertTrue(StorageAccess.write(context, storage + "/tone.wav", tone.toByteArray()));
            }
            write(storage + "/compiled-source.tjs", program);
            write(storage + "/psb-tests.tjs", read(tests.getAssets().open("engine/psb-tests.tjs")));
            write(storage + "/datapack-tests.tjs", read(tests.getAssets().open("engine/datapack-tests.tjs")));
            write(storage + "/plugin-tests.tjs", read(tests.getAssets().open("engine/plugin-tests.tjs")));
            write(storage + "/kag-parser-tests.tjs", read(tests.getAssets().open("engine/kag-parser-tests.tjs")));
            write(storage + "/layer-viewport.tjs", read(tests.getAssets().open("engine/layer-viewport.tjs")));
            write(storage + "/viewport-tests.tjs", read(tests.getAssets().open("engine/viewport-tests.tjs")));
            write(storage + "/integer-scale-tests.tjs", read(tests.getAssets().open("engine/integer-scale-tests.tjs")));
            assertTrue(StorageAccess.write(context, storage + "/plugin-tone.wav",
                    readBytes(tests.getAssets().open("engine/plugin-tone.wav"))));
            if (name.startsWith("transitions"))
                assertTrue(StorageAccess.write(context, storage + "/transition-rule.bmp",
                        readBytes(tests.getAssets().open("engine/transition-rule.bmp"))));
            for (String filename : tests.getAssets().list("datapack")) {
                assertTrue(StorageAccess.write(context, storage + "/pack-" + filename,
                        readBytes(tests.getAssets().open("datapack/" + filename))));
            }
            for (String filename : tests.getAssets().list("psb")) {
                try (InputStream source = tests.getAssets().open("psb/" + filename);
                     ByteArrayOutputStream bytes = new ByteArrayOutputStream()) {
                    byte[] buffer = new byte[4096]; int count;
                    while ((count = source.read(buffer)) != -1) bytes.write(buffer, 0, count);
                    assertTrue(StorageAccess.write(context, storage + "/" + filename, bytes.toByteArray()));
                }
            }
            if (name.startsWith("archive")) {
                assertTrue(StorageAccess.mkdirs(context, storage + "/loading-loose"));
                assertTrue(StorageAccess.delete(context, storage + "/loading-loose/ci-loading-created.txt"));
                assertTrue(StorageAccess.mkdirs(context, storage + "/loading-paths/subdir"));
                assertTrue(StorageAccess.mkdirs(context, storage + "/LoadingAncestor/NestedDir"));
                write(storage + "/loading-paths/subdir/Mixed.TJS", "global.ciLoadingValue = 71;");
                write(storage + "/LoadingAncestor/NestedDir/CaseScene.TJS", "global.ciLoadingValue = 72;");
                assertTrue(StorageAccess.delete(context, storage + "/loading-paths/subdir/NewScene.TJS"));
                String[] archives = tests.getAssets().list("archives");
                assertNotNull(archives);
                assertTrue("synthetic archive fixtures are packaged", archives.length >= 21);
                for (String archive : archives) {
                    try (InputStream input = tests.getAssets().open("archives/" + archive);
                         ByteArrayOutputStream bytes = new ByteArrayOutputStream()) {
                        byte[] buffer = new byte[4096]; int size;
                        while ((size = input.read(buffer)) != -1) bytes.write(buffer, 0, size);
                        assertTrue(StorageAccess.write(context, storage + "/" + archive, bytes.toByteArray()));
                    }
                }
            }
            assertTrue(StorageAccess.write(context, storage + "/broken.tjb", new byte[] {'T','J','S','2','1','0','0',0}));
            write(storage + "/unsafe.txt", "(global.ciSideEffect = 1, %[]) ");
            write(storage + "/preprocessor.txt", "@set(ciSideEffect=1) (const) %[]");
            manifest.append(name).append('\t').append(storage)
                .append(name.startsWith("archive-launch") ? "/launch-path.xp3" : "")
                .append('\t').append(output).append('\n');
            if (name.equals("local") || name.equals("documents"))
                manifest.append("compiled-").append(name).append('\t').append(storage).append('\t').append(output).append('\n');
        }
        write(new File(context.getFilesDir(), "engine-ci-cases.txt").getPath(), manifest.toString());
    }
}
