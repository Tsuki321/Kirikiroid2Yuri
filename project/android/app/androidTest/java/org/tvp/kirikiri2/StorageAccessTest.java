package org.tvp.kirikiri2;

import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
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
    private static String read(InputStream input) throws Exception {
        try (InputStream stream = input; ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096]; int count;
            while ((count = stream.read(buffer)) != -1) output.write(buffer, 0, count);
            return new String(output.toByteArray(), StandardCharsets.UTF_8);
        }
    }
    private static void write(String path, String text) { assertTrue(path, StorageAccess.write(context, path, text.getBytes(StandardCharsets.UTF_8))); }
    @BeforeClass public static void setup() {
        context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        Bundle request = new Bundle(); request.putString("package", context.getPackageName());
        Bundle response = context.getContentResolver().call(Uri.parse("content://" + TestDocumentsProvider.AUTHORITY), "grantFixture", null, request);
        assertNotNull(response);
        Uri tree = Uri.parse(response.getString("tree"));
        context.getContentResolver().takePersistableUriPermission(tree,
            Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
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
    @Test public void zPrepareEngineFixturesForProcessRestart() throws Exception {
        Context tests = InstrumentationRegistry.getInstrumentation().getContext();
        String script = read(tests.getAssets().open("engine/startup.tjs"));
        String program = read(tests.getAssets().open("engine/compiled-source.tjs"));
        File base = new File(context.getFilesDir(), "engine-ci");
        assertTrue(StorageAccess.mkdirs(context, base.getPath()));
        StringBuilder manifest = new StringBuilder();
        String[] names = {"local", "documents"};
        for (String name : names) {
            String storage = name.equals("local") ? new File(base, name).getPath() : documents + "/Engine";
            String output = new File(base, name + "-result").getPath();
            assertTrue(StorageAccess.mkdirs(context, storage));
            assertTrue(StorageAccess.mkdirs(context, output));
            write(storage + "/startup.tjs", script.replace("@@STORAGE@@", storage).replace("@@OUTPUT@@", output));
            write(storage + "/compiled-source.tjs", program);
            assertTrue(StorageAccess.write(context, storage + "/broken.tjb", new byte[] {'T','J','S','2','1','0','0',0}));
            write(storage + "/unsafe.txt", "(global.ciSideEffect = 1, %[]) ");
            write(storage + "/preprocessor.txt", "@set(ciSideEffect=1) (const) %[]");
            manifest.append(name).append('\t').append(storage).append('\t').append(output).append('\n');
        }
        write(new File(context.getFilesDir(), "engine-ci-cases.txt").getPath(), manifest.toString());
    }
}
