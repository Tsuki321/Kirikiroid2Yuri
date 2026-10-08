package com.yuri.kirikiri2;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import org.tvp.kirikiri2.GameLibrary;

/** Keep all library writes in its own process, including confirmed game launches. */
public final class LibraryHistoryReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context, Intent intent) {
        String id = intent.getStringExtra("library_entry_id");
        if (id == null) return;
        GameLibrary library = new GameLibrary(context);
        for (GameLibrary.Entry entry : library.load()) {
            if (entry.id().equals(id)) {
                entry.lastPlayed = System.currentTimeMillis();
                library.put(entry);
                return;
            }
        }
    }
}
