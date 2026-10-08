package org.tvp.kirikiri2;

import android.database.Cursor;
import android.database.MatrixCursor;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.DocumentsProvider;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;

/** A real, separate-UID document provider installed only with the test APK. */
public class TestDocumentsProvider extends DocumentsProvider {
    static final String AUTHORITY = "com.yuri.kirikiri2.tests.documents";
    private static final String[] COLUMNS = {
        DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_MIME_TYPE, DocumentsContract.Document.COLUMN_FLAGS,
        DocumentsContract.Document.COLUMN_SIZE, DocumentsContract.Document.COLUMN_LAST_MODIFIED
    };
    private File root;
    private android.os.HandlerThread proxyThread;
    @Override public boolean onCreate() {
        root = new File(getContext().getFilesDir(), "documents");
        proxyThread = new android.os.HandlerThread("FixtureFiles");
        proxyThread.start();
        return root.isDirectory() || root.mkdirs();
    }
    private File file(String id) throws FileNotFoundException {
        try {
            if (!(id.equals("root") || id.startsWith("root/"))) throw new IOException("Unknown document");
            File result = id.equals("root") ? root : new File(root, id.substring(5));
            if (!result.getCanonicalPath().equals(root.getCanonicalPath())
                    && !result.getCanonicalPath().startsWith(root.getCanonicalPath() + "/"))
                throw new IOException("Escaping document root");
            return result;
        } catch (IOException e) { throw new FileNotFoundException(e.getMessage()); }
    }
    private String id(File file) { return "root" + file.getAbsolutePath().substring(root.getAbsolutePath().length()); }
    private void add(MatrixCursor cursor, File file) {
        MatrixCursor.RowBuilder row = cursor.newRow();
        for (String column : cursor.getColumnNames()) {
            Object value = null;
            if (column.equals(COLUMNS[0])) value = id(file);
            if (column.equals(COLUMNS[1])) value = file.getName();
            if (column.equals(COLUMNS[2])) value = file.isDirectory() ? DocumentsContract.Document.MIME_TYPE_DIR : "application/octet-stream";
            if (column.equals(COLUMNS[3])) value = DocumentsContract.Document.FLAG_SUPPORTS_WRITE
                    | DocumentsContract.Document.FLAG_SUPPORTS_DELETE | DocumentsContract.Document.FLAG_SUPPORTS_RENAME
                    | (file.isDirectory() ? DocumentsContract.Document.FLAG_DIR_SUPPORTS_CREATE : 0);
            if (column.equals(COLUMNS[4])) value = file.length();
            if (column.equals(COLUMNS[5])) value = file.lastModified();
            row.add(value);
        }
    }
    @Override public Cursor queryRoots(String[] projection) {
        String[] columns = projection == null ? new String[] {
            DocumentsContract.Root.COLUMN_ROOT_ID, DocumentsContract.Root.COLUMN_DOCUMENT_ID,
            DocumentsContract.Root.COLUMN_TITLE, DocumentsContract.Root.COLUMN_FLAGS
        } : projection;
        MatrixCursor result = new MatrixCursor(columns);
        MatrixCursor.RowBuilder row = result.newRow();
        for (String column : columns) {
            if (column.equals(DocumentsContract.Root.COLUMN_ROOT_ID)) row.add("games");
            else if (column.equals(DocumentsContract.Root.COLUMN_DOCUMENT_ID)) row.add("root/Games");
            else if (column.equals(DocumentsContract.Root.COLUMN_TITLE)) row.add("Kirikiri test games");
            else if (column.equals(DocumentsContract.Root.COLUMN_FLAGS)) row.add(DocumentsContract.Root.FLAG_SUPPORTS_CREATE
                    | DocumentsContract.Root.FLAG_LOCAL_ONLY | DocumentsContract.Root.FLAG_SUPPORTS_IS_CHILD);
            else row.add(null);
        }
        return result;
    }

    @Override public boolean isChildDocument(String parent, String child) {
        try {
            return file(child).getCanonicalPath().startsWith(file(parent).getCanonicalPath() + File.separator);
        } catch (IOException error) { return false; }
    }
    @Override public Cursor queryDocument(String documentId, String[] projection) throws FileNotFoundException {
        MatrixCursor result = new MatrixCursor(projection == null ? COLUMNS : projection);
        File file = file(documentId);
        if (file.exists()) add(result, file);
        return result;
    }
    @Override public Cursor queryChildDocuments(String parent, String[] projection, String sort) throws FileNotFoundException {
        MatrixCursor result = new MatrixCursor(projection == null ? COLUMNS : projection);
        File[] children = file(parent).listFiles();
        if (children != null) for (File child : children) add(result, child);
        return result;
    }
    @Override public boolean isChildDocument(String parent, String child) { return child.startsWith(parent + "/"); }
    @Override public ParcelFileDescriptor openDocument(String id, String mode, CancellationSignal signal) throws FileNotFoundException {
        if (id.endsWith("/full.dat") && mode.contains("w")) {
            android.os.storage.StorageManager storage = (android.os.storage.StorageManager)
                getContext().getSystemService(android.content.Context.STORAGE_SERVICE);
            try {
                return storage.openProxyFileDescriptor(ParcelFileDescriptor.MODE_READ_WRITE,
                    new android.os.ProxyFileDescriptorCallback() {
                        @Override public long onGetSize() { return 0; }
                        @Override public int onRead(long offset, int size, byte[] data) { return 0; }
                        @Override public int onWrite(long offset, int size, byte[] data) throws android.system.ErrnoException {
                            if (offset + size > 32) throw new android.system.ErrnoException("write", android.system.OsConstants.ENOSPC);
                            return size;
                        }
                        @Override public void onFsync() {}
                        @Override public void onRelease() {}
                    }, new android.os.Handler(proxyThread.getLooper()));
            } catch (IOException e) { throw new FileNotFoundException(e.getMessage()); }
        }
        return ParcelFileDescriptor.open(file(id), ParcelFileDescriptor.parseMode(mode));
    }
    @Override public String createDocument(String parent, String mime, String name) throws FileNotFoundException {
        if (name.contains("/") || name.equals(".") || name.equals("..")) throw new FileNotFoundException("Invalid name");
        File child = new File(file(parent), name);
        try {
            boolean ok = DocumentsContract.Document.MIME_TYPE_DIR.equals(mime) ? child.mkdirs() : child.createNewFile();
            if (!ok && !child.exists()) throw new IOException("Cannot create fixture");
            return id(child);
        } catch (IOException e) { throw new FileNotFoundException(e.getMessage()); }
    }
    private void remove(File file) throws FileNotFoundException {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) remove(child);
        if (!file.delete()) throw new FileNotFoundException("Cannot delete fixture");
    }
    @Override public void deleteDocument(String id) throws FileNotFoundException { remove(file(id)); }
    @Override public String renameDocument(String document, String name) throws FileNotFoundException {
        File before = file(document), after = new File(before.getParentFile(), name);
        if (name.contains("/") || !before.renameTo(after)) throw new FileNotFoundException("Cannot rename fixture");
        return id(after);
    }
}
