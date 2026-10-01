package org.tvp.kirikiri2;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.provider.DocumentsContract;
import java.io.File;

/** Grants from the provider's owning UID, just as a document picker does. */
public class GrantDocumentActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        File games = new File(getFilesDir(), "documents/Games");
        if (!games.isDirectory() && !games.mkdirs()) throw new IllegalStateException("Cannot create fixture tree");
        grantUriPermission("com.yuri.kirikiri2",
            DocumentsContract.buildTreeDocumentUri(TestDocumentsProvider.AUTHORITY, "root/Games"),
            Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        finish();
    }
}
