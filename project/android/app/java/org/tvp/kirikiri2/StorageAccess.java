package org.tvp.kirikiri2;

import android.content.Context;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.util.AtomicFile;
import android.util.Log;
import androidx.documentfile.provider.DocumentFile;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** Filesystem-shaped access to explicitly granted document trees. */
public final class StorageAccess {
    private static final String TAG = "StorageAccess";
    private static final String MOUNT = "/documents/";
    private StorageAccess() {}

    private static final class Tree {
        final DocumentFile document;
        final String mount;
        final String localRoot;
        Tree(Context context, Uri uri) throws Exception {
            document = DocumentFile.fromTreeUri(context, uri);
            if (document == null) throw new IOException("Invalid document tree");
            byte[] digest = MessageDigest.getInstance("SHA-256").digest(uri.toString().getBytes("UTF-8"));
            StringBuilder id = new StringBuilder();
            for (int i = 0; i < 12; ++i) id.append(String.format(Locale.ROOT, "%02x", digest[i] & 255));
            String label = document.getName();
            if (label == null || label.length() == 0) label = "Folder";
            label = label.replaceAll("[\\\\/:\u0000]", "_");
            mount = MOUNT + label + "-" + id;
            String root = null;
            if ("com.android.externalstorage.documents".equals(uri.getAuthority())) {
                String treeId = DocumentsContract.getTreeDocumentId(uri);
                int colon = treeId.indexOf(':');
                if (colon >= 0) {
                    String volume = treeId.substring(0, colon);
                    root = ("primary".equalsIgnoreCase(volume)
                            ? Environment.getExternalStorageDirectory().getAbsolutePath()
                            : "/storage/" + volume) + "/" + treeId.substring(colon + 1);
                    root = new File(root).getCanonicalPath();
                }
            }
            localRoot = root;
        }
    }

    private static List<Tree> trees(Context context) {
        List<Tree> result = new ArrayList<>();
        for (UriPermission grant : context.getContentResolver().getPersistedUriPermissions()) {
            if (!grant.isReadPermission()) continue;
            try { result.add(new Tree(context, grant.getUri())); }
            catch (Exception e) { Log.w(TAG, "Document grant is no longer available", e); }
        }
        return result;
    }

    public static String[] roots(Context context) {
        List<String> result = new ArrayList<>();
        for (Tree tree : trees(context)) result.add(tree.mount);
        return result.toArray(new String[0]);
    }

    private static boolean under(String path, String root) {
        return path.equalsIgnoreCase(root) || path.regionMatches(true, 0, root + "/", 0, root.length() + 1);
    }

    static DocumentFile resolve(Context context, String path, boolean create, boolean directory) throws IOException {
        if (path == null) return null;
        String canonical = new File(path).getCanonicalPath();
        Tree selected = null;
        String relative = null;
        int longest = -1;
        for (Tree tree : trees(context)) {
            String root = under(canonical, tree.mount) ? tree.mount : tree.localRoot;
            if (root != null && under(canonical, root) && root.length() > longest) {
                selected = tree;
                relative = canonical.substring(root.length());
                longest = root.length();
            }
        }
        if (selected == null) return null;
        DocumentFile current = selected.document;
        String[] parts = relative.split("/");
        for (int i = 0; i < parts.length; ++i) {
            if (parts[i].length() == 0) continue;
            if (".".equals(parts[i]) || "..".equals(parts[i])) throw new IOException("Invalid document path");
            DocumentFile next = current.findFile(parts[i]);
            // Kirikiri normalizes ASCII case. Preserve the provider's spelling.
            if (next == null) {
                for (DocumentFile child : current.listFiles()) {
                    if (parts[i].equalsIgnoreCase(child.getName())) { next = child; break; }
                }
            }
            if (next == null && create) {
                next = (i < parts.length - 1 || directory)
                        ? current.createDirectory(parts[i])
                        : current.createFile("application/octet-stream", parts[i]);
            }
            if (next == null) return null;
            current = next;
        }
        return current;
    }

    public static int open(Context context, String path, int access) {
        if (access < 0 || access > 3) return -1;
        try {
            DocumentFile doc = resolve(context, path, access == 1 || access == 2, false);
            if (doc == null || doc.isDirectory()) return -1;
            String mode = access == 0 ? "r" : access == 1 ? "rwt" : "rw";
            try (ParcelFileDescriptor descriptor = context.getContentResolver().openFileDescriptor(doc.getUri(), mode)) {
                if (descriptor == null) return -1;
                return descriptor.detachFd();
            }
        } catch (Exception e) { Log.w(TAG, "Cannot open document", e); return -1; }
    }

    public static long[] stat(Context context, String path) {
        try {
            DocumentFile doc = resolve(context, path, false, false);
            if (doc == null || !doc.exists()) return null;
            return new long[] { doc.isDirectory() ? 0040000 : 0100000, doc.length(), doc.lastModified() / 1000 };
        } catch (Exception e) { Log.w(TAG, "Cannot query document", e); return null; }
    }

    // Four fields per entry avoid JNI lookups for every metadata field.
    public static String[] list(Context context, String path) {
        try {
            DocumentFile doc = resolve(context, path, false, true);
            if (doc == null || !doc.isDirectory()) return null;
            List<String> result = new ArrayList<>();
            for (DocumentFile child : doc.listFiles()) {
                String name = child.getName();
                if (name == null || name.contains("/") || name.equals(".") || name.equals("..")) continue;
                result.add(name);
                result.add(child.isDirectory() ? "d" : "f");
                result.add(Long.toString(child.length()));
                result.add(Long.toString(child.lastModified() / 1000));
            }
            return result.toArray(new String[0]);
        } catch (Exception e) { Log.w(TAG, "Cannot list documents", e); return null; }
    }

    public static boolean mkdirs(Context context, String path) {
        if (path == null || path.length() == 0) return false;
        try {
            File directory = new File(path);
            if (directory.isDirectory() || directory.mkdirs()) return true;
            DocumentFile doc = resolve(context, path, true, true);
            return doc != null && doc.isDirectory();
        } catch (Exception e) { Log.w(TAG, "Cannot create directory", e); return false; }
    }

    public static boolean write(Context context, String path, byte[] data) {
        if (path == null || data == null) return false;
        File file = new File(path);
        File parent = file.getParentFile();
        if (parent != null && !mkdirs(context, parent.getPath())) return false;
        // AtomicFile keeps the previous local file if writing or closing fails.
        AtomicFile atomic = new AtomicFile(file);
        FileOutputStream output = null;
        try {
            output = atomic.startWrite();
            output.write(data);
            output.getFD().sync();
            atomic.finishWrite(output);
            return true;
        } catch (Exception e) { if (output != null) atomic.failWrite(output); }
        try {
            DocumentFile doc = resolve(context, path, true, false);
            if (doc == null || doc.isDirectory()) return false;
            try (OutputStream stream = context.getContentResolver().openOutputStream(doc.getUri(), "rwt")) {
                if (stream == null) return false;
                stream.write(data);
                stream.flush();
            }
            return true;
        } catch (Exception e) { Log.w(TAG, "Cannot write document", e); return false; }
    }

    // Engine file and directory removal has unlink/rmdir semantics. The file
    // browser's recursive delete operation below is deliberately separate.
    public static boolean removeDocument(Context context, String path, boolean directory) {
        try {
            DocumentFile doc = resolve(context, path, false, directory);
            if (doc == null || !doc.exists() || doc.isDirectory() != directory) return false;
            if (directory) {
                Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(
                    doc.getUri(), DocumentsContract.getDocumentId(doc.getUri()));
                try (Cursor cursor = context.getContentResolver().query(children,
                        new String[] {DocumentsContract.Document.COLUMN_DOCUMENT_ID}, null, null, null)) {
                    if (cursor == null || cursor.moveToFirst()) return false;
                }
            }
            return doc.delete();
        } catch (Exception e) { Log.w(TAG, "Cannot remove document", e); return false; }
    }

    private static boolean deleteLocal(File file) throws IOException {
        if (file.isDirectory() && file.getCanonicalFile().equals(file.getAbsoluteFile())) {
            File[] children = file.listFiles();
            if (children == null) return false;
            for (File child : children) if (!deleteLocal(child)) return false;
        }
        return file.delete();
    }

    public static boolean delete(Context context, String path) {
        try {
            File file = new File(path);
            if (file.exists() && deleteLocal(file)) return true;
            DocumentFile doc = resolve(context, path, false, false);
            return doc != null ? doc.delete() : !file.exists();
        } catch (Exception e) { Log.w(TAG, "Cannot delete document", e); return false; }
    }

    public static boolean rename(Context context, String from, String to) {
        try {
            File source = new File(from), target = new File(to);
            if (source.renameTo(target)) return true;
            if (!source.getParentFile().getCanonicalPath().equalsIgnoreCase(target.getParentFile().getCanonicalPath()))
                return false; // Do not silently turn an unsupported move into data loss.
            DocumentFile doc = resolve(context, from, false, false);
            if (doc == null || resolve(context, to, false, false) != null) return false;
            return doc.renameTo(target.getName());
        } catch (Exception e) { Log.w(TAG, "Cannot rename document", e); return false; }
    }
}
