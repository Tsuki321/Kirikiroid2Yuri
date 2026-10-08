package com.yuri.kirikiri2;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.provider.DocumentsContract;
import android.text.Editable;
import android.text.TextWatcher;
import android.text.format.DateUtils;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import org.tvp.kirikiri2.GameLibrary;
import org.tvp.kirikiri2.ModernUi;
import org.tvp.kirikiri2.StorageAccess;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import static org.tvp.kirikiri2.ModernUi.*;

/** A native, accessible launcher which stays alive when the engine exits. */
public class LibraryActivity extends Activity {
    private static final int PICK_FOLDER = 410;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private GameLibrary library;
    private LinearLayout content, cards, recent;
    private EditText search;
    private Button add, sort;
    private TextView status;
    private ProgressBar progress;
    private ScrollView scroll;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private boolean launchedGame;
    private String query = "";
    private int order;
    private GameLibrary.Entry relocating;
    private boolean busy;
    private String busyMessage;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        ModernUi.systemBars(this);
        library = new GameLibrary(this);
        order = getPreferences(MODE_PRIVATE).getInt("order", 0);
        if (state != null) {
            query = state.getString("query", "");
            order = state.getInt("order");
            String relocateId = state.getString("relocate");
            for (GameLibrary.Entry entry : library.load())
                if (entry.id().equals(relocateId)) relocating = entry;
        }
        buildView();
        render();
        handlePendingGame(getIntent());
    }

    @Override protected void onResume() {
        super.onResume();
        if (library != null) render();
        if (launchedGame) { launchedGame = false; scroll.post(() -> scroll.scrollTo(0, 0)); }
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        state.putString("query", query);
        state.putInt("order", order);
        if (relocating != null) state.putString("relocate", relocating.id());
        super.onSaveInstanceState(state);
    }

    @Override protected void onDestroy() { worker.shutdownNow(); handler.removeCallbacksAndMessages(null); super.onDestroy(); }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent); setIntent(intent); handlePendingGame(intent);
        scroll.post(() -> scroll.scrollTo(0, 0));
    }

    private void handlePendingGame(Intent intent) {
        String path = intent.getStringExtra("pending_game");
        String entryId = intent.getStringExtra("pending_entry");
        int previousPid = intent.getIntExtra("exiting_engine", -1);
        intent.removeExtra("pending_game"); intent.removeExtra("exiting_engine"); intent.removeExtra("pending_entry");
        if (path == null || previousPid <= 0) return;
        busy = true; busyMessage = "Closing the previous game…";
        add.setEnabled(false); progress.setVisibility(View.VISIBLE); status.setText(busyMessage);
        long deadline = SystemClock.uptimeMillis() + 10000;
        handler.post(new Runnable() {
            @Override public void run() {
                if (isFinishing() || isDestroyed()) return;
                ActivityManager manager = (ActivityManager)getSystemService(ACTIVITY_SERVICE);
                List<ActivityManager.RunningAppProcessInfo> processes = manager.getRunningAppProcesses();
                boolean running = processes == null;
                if (processes != null) for (ActivityManager.RunningAppProcessInfo process : processes)
                    if (process.pid == previousPid) running = true;
                if (running && SystemClock.uptimeMillis() < deadline) { handler.postDelayed(this, 100); return; }
                busy = false; add.setEnabled(true); progress.setVisibility(View.GONE); render();
                if (running) showError("The previous game is still closing", "Wait a moment, then tap Play again.");
                else startPlayer(path.isEmpty() ? null : path, entryId);
            }
        });
    }

    @Override public void onConfigurationChanged(Configuration configuration) {
        super.onConfigurationChanged(configuration);
        buildView(); render();
        add.setEnabled(!busy);
        progress.setVisibility(busy ? View.VISIBLE : View.GONE);
        if (busy) status.setText(busyMessage);
    }

    private void buildView() {
        scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(BACKGROUND);
        FrameLayout center = new FrameLayout(this);
        content = column(this, 24);
        int width = Math.min(getResources().getDisplayMetrics().widthPixels, dp(this, 860));
        FrameLayout.LayoutParams centered = new FrameLayout.LayoutParams(width, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        center.addView(content, centered);
        scroll.addView(center);
        setContentView(scroll);

        LinearLayout top = row(this);
        TextView brand = text(this, "KIRIKIROID", 12, ACCENT);
        brand.setLetterSpacing(0.18f);
        top.addView(brand, new LinearLayout.LayoutParams(0, -2, 1));
        Button help = button(this, "Help", false);
        help.setOnClickListener(view -> showHelp());
        top.addView(help);
        content.addView(top);
        gap(content, 26);
        content.addView(title(this, "Your library", 32));
        gap(content, 6);
        content.addView(text(this, "Pick a story. Settle in.", 16, MUTED));
        gap(content, 22);
        recent = column(this, 22);
        recent.setBackground(shape(this, SURFACE, 22));
        content.addView(recent, new LinearLayout.LayoutParams(-1, -2));
        gap(content, 18);

        add = button(this, "+  Add game folder", true);
        add.setOnClickListener(view -> { relocating = null; pickFolder(null); });
        content.addView(add, new LinearLayout.LayoutParams(-1, -2));
        gap(content, 16);

        LinearLayout filters = row(this);
        search = new EditText(this);
        search.setSingleLine(true);
        search.setTextColor(TEXT);
        search.setHintTextColor(MUTED);
        search.setTextSize(15);
        search.setHint("Search your games");
        search.setContentDescription("Search your games");
        search.setImeOptions(EditorInfo.IME_ACTION_SEARCH);
        search.setPadding(dp(this, 14), 0, dp(this, 14), 0);
        search.setBackground(shape(this, SURFACE, 14));
        search.setText(query);
        filters.addView(search, new LinearLayout.LayoutParams(0, dp(this, 52), 1));
        sort = button(this, sortLabel(), false);
        LinearLayout.LayoutParams sortParams = new LinearLayout.LayoutParams(-2, dp(this, 52));
        sortParams.leftMargin = dp(this, 8);
        filters.addView(sort, sortParams);
        sort.setContentDescription("Sort and filter games");
        sort.setOnClickListener(view -> {
            PopupMenu menu = new PopupMenu(this, sort);
            String[] labels = {"Recent", "A–Z", "Favorites"};
            for (int i = 0; i < labels.length; ++i) menu.getMenu().add(0, i, i, labels[i]);
            menu.setOnMenuItemClickListener(item -> {
                order = item.getItemId(); getPreferences(MODE_PRIVATE).edit().putInt("order", order).apply();
                sort.setText(sortLabel()); render(); return true;
            });
            menu.show();
        });
        content.addView(filters);
        gap(content, 12);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setIndeterminate(true);
        progress.setVisibility(View.GONE);
        content.addView(progress, new LinearLayout.LayoutParams(-1, dp(this, 4)));
        status = text(this, "", 13, MUTED);
        status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        content.addView(status);
        gap(content, 10);
        cards = column(this, 0);
        content.addView(cards);
        gap(content, 16);
        Button advanced = button(this, "Open classic file browser", false);
        advanced.setOnClickListener(view -> startPlayer(null));
        content.addView(advanced);
        search.addTextChangedListener(new TextWatcher() {
            public void beforeTextChanged(CharSequence s, int start, int count, int after) { }
            public void onTextChanged(CharSequence s, int start, int before, int count) { query = s.toString(); renderCards(); }
            public void afterTextChanged(Editable value) { }
        });
    }

    private String sortLabel() { return new String[] {"Recent", "A–Z", "Favorites"}[Math.max(0, Math.min(2, order))]; }

    private void render() {
        recent.removeAllViews();
        List<GameLibrary.Entry> entries = library.load();
        GameLibrary.Entry latest = null;
        for (GameLibrary.Entry entry : entries)
            if (entry.lastPlayed > 0 && (latest == null || entry.lastPlayed > latest.lastPlayed)) latest = entry;
        if (latest == null) {
            recent.addView(text(this, entries.isEmpty() ? "A PLACE FOR YOUR STORIES" : "READY WHEN YOU ARE", 11, ACCENT));
            gap(recent, 12);
            recent.addView(title(this, entries.isEmpty() ? "Your next story starts here." : "Your games, one tap away.", 23));
            gap(recent, 8);
            recent.addView(text(this, "Add an extracted Kirikiri game folder once. Keep its archives, patches and support files together.", 15, MUTED));
        } else {
            final GameLibrary.Entry entry = latest;
            recent.addView(text(this, "LAST PLAYED", 11, ACCENT));
            gap(recent, 10);
            recent.addView(title(this, entry.name, 24));
            gap(recent, 6);
            recent.addView(text(this, "Launch the game, then load your save from its menu.", 14, MUTED));
            gap(recent, 16);
            Button play = button(this, "Play again", true);
            play.setOnClickListener(view -> play(entry));
            recent.addView(play, new LinearLayout.LayoutParams(-1, -2));
        }
        renderCards();
    }

    private void renderCards() {
        cards.removeAllViews();
        List<GameLibrary.Entry> all = library.load();
        List<GameLibrary.Entry> entries = GameLibrary.filter(all, query, order);
        if (!busy) status.setText(entries.size() + (entries.size() == 1 ? " game" : " games"));
        if (entries.isEmpty()) {
            TextView empty = text(this, all.isEmpty() ? "No games added yet. Use Add game folder to get started."
                    : order == 2 && query.isEmpty() ? "No favorites yet. Open a game’s options to add one."
                    : "No matching games. Try another search or filter.", 15, MUTED);
            empty.setPadding(0, dp(this, 18), 0, dp(this, 22));
            cards.addView(empty);
        }
        for (GameLibrary.Entry entry : entries) {
            LinearLayout card = row(this);
            card.setPadding(dp(this, 16), dp(this, 12), dp(this, 10), dp(this, 12));
            card.setBackground(shape(this, SURFACE, 18));
            LinearLayout details = column(this, 0);
            details.addView(title(this, (entry.favorite ? "★  " : "") + entry.name, 18));
            gap(details, 5);
            String location = entry.launchFile.isEmpty() ? "Game folder" : entry.launchFile;
            String played = entry.lastPlayed == 0 ? "Ready to launch" : "Played " + DateUtils.getRelativeTimeSpanString(entry.lastPlayed, System.currentTimeMillis(), DateUtils.MINUTE_IN_MILLIS);
            details.addView(text(this, location + "  ·  " + played, 12, MUTED));
            details.setPadding(0, dp(this, 8), dp(this, 8), dp(this, 8));
            details.setContentDescription("Play " + entry.name);
            details.setFocusable(true);
            details.setOnClickListener(view -> play(entry));
            card.addView(details, new LinearLayout.LayoutParams(0, -2, 1));
            Button play = button(this, "Play", true);
            play.setContentDescription("Play " + entry.name);
            play.setOnClickListener(view -> play(entry));
            card.addView(play);
            Button more = button(this, "⋮", false);
            more.setTextSize(22);
            more.setContentDescription("Options for " + entry.name);
            more.setOnClickListener(view -> entryMenu(entry, more));
            LinearLayout.LayoutParams options = new LinearLayout.LayoutParams(dp(this, 48), dp(this, 48));
            options.leftMargin = dp(this, 8);
            card.addView(more, options);
            cards.addView(card, new LinearLayout.LayoutParams(-1, -2));
            gap(cards, 10);
        }
    }

    private void entryMenu(GameLibrary.Entry entry, View anchor) {
        PopupMenu menu = new PopupMenu(this, anchor);
        menu.getMenu().add(0, 0, 0, entry.favorite ? "Remove favorite" : "Add to favorites");
        menu.getMenu().add(0, 1, 1, "Rename");
        menu.getMenu().add(0, 2, 2, "Choose launch file");
        menu.getMenu().add(0, 3, 3, "Locate folder again");
        menu.getMenu().add(0, 4, 4, "Remove from library");
        menu.setOnMenuItemClickListener(item -> {
            switch (item.getItemId()) {
                case 0: entry.favorite = !entry.favorite; library.put(entry); render(); break;
                case 1: rename(entry); break;
                case 2: runWork("Checking launch files…", () -> GameLibrary.launchFiles(this, entry), files -> chooseLaunch(entry, files, () -> { library.put(entry); render(); })); break;
                case 3: relocating = entry; pickFolder(Uri.parse(entry.tree)); break;
                case 4: new AlertDialog.Builder(this).setTitle("Remove “" + entry.name + "”?")
                        .setMessage("This only removes the library shortcut. Your game files and saves stay in their folder.")
                        .setNegativeButton("Keep game", null).setPositiveButton("Remove", (dialog, which) -> { library.remove(entry); render(); }).show(); break;
            }
            return true;
        });
        menu.show();
    }

    private void rename(GameLibrary.Entry entry) {
        EditText name = new EditText(this);
        name.setSingleLine(true);
        name.setText(entry.name);
        name.setSelectAllOnFocus(true);
        LinearLayout box = column(this, 24);
        box.addView(name);
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle("Game name").setView(box)
                .setNegativeButton("Cancel", null).setPositiveButton("Save", null).create();
        dialog.setOnShowListener(ignored -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
            String value = name.getText().toString().trim();
            if (value.isEmpty()) { name.setError("Enter a name"); return; }
            entry.name = value; library.put(entry); render(); dialog.dismiss();
        }));
        dialog.show();
    }

    private void pickFolder(Uri initial) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        if (Build.VERSION.SDK_INT >= 26 && initial != null) intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, initial);
        try { startActivityForResult(intent, PICK_FOLDER); }
        catch (ActivityNotFoundException error) { showError("Folder picker unavailable", "Enable the system Files app, then try adding the folder again. The classic file browser is also available below your library."); }
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_FOLDER) return;
        if (result != RESULT_OK || data == null || data.getData() == null) { relocating = null; return; }
        Uri tree = data.getData();
        try {
            int flags = data.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            if ((flags & Intent.FLAG_GRANT_READ_URI_PERMISSION) == 0) throw new SecurityException("No read access");
            getContentResolver().takePersistableUriPermission(tree, flags);
        } catch (SecurityException error) {
            showError("Folder access wasn’t saved", "Select the folder in the system Files picker and allow access so it can be opened next time.");
            relocating = null;
            return;
        }
        final GameLibrary.Entry previous = relocating;
        relocating = null;
        runWork("Looking for games…", () -> GameLibrary.discover(this, tree), folders -> {
            if (folders.size() == 1) addDiscovered(folders.get(0), previous);
            else {
                String[] names = new String[folders.size()];
                for (int i = 0; i < names.length; ++i) names[i] = folders.get(i).entry.name;
                new AlertDialog.Builder(this).setTitle("Choose a game folder")
                        .setItems(names, (dialog, which) -> addDiscovered(folders.get(which), previous))
                        .setNegativeButton("Cancel", null).show();
            }
        });
    }

    private void addDiscovered(GameLibrary.Folder folder, GameLibrary.Entry previous) {
        GameLibrary.Entry entry = folder.entry;
        Runnable save = () -> {
            for (GameLibrary.Entry existing : library.load()) {
                if (existing.id().equals(entry.id()) && (previous == null || !existing.id().equals(previous.id()))) {
                    Toast.makeText(this, "This game is already in your library", Toast.LENGTH_SHORT).show();
                    render();
                    return;
                }
            }
            if (previous != null) {
                entry.name = previous.name;
                entry.favorite = previous.favorite;
                entry.lastPlayed = previous.lastPlayed;
                library.remove(previous);
            }
            library.put(entry);
            query = ""; order = 0; search.setText(""); sort.setText(sortLabel()); render();
            Toast.makeText(this, "Added " + entry.name, Toast.LENGTH_SHORT).show();
        };
        if (folder.needsChoice()) chooseLaunch(entry, folder.candidates, save); else save.run();
    }

    private void chooseLaunch(GameLibrary.Entry entry, List<String> files, Runnable complete) {
        String[] labels = new String[files.size()];
        for (int i = 0; i < labels.length; ++i) labels[i] = files.get(i).isEmpty() ? "startup.tjs (game folder)" : files.get(i);
        new AlertDialog.Builder(this).setTitle("Choose the game’s startup file")
                .setItems(labels, (dialog, which) -> { entry.launchFile = files.get(which); complete.run(); })
                .setNegativeButton("Cancel", null).show();
    }

    private void play(GameLibrary.Entry entry) {
        runWork("Opening “" + entry.name + "”…", () -> {
            String path = entry.path(this);
            if (StorageAccess.stat(this, path) == null)
                throw new IOException("The game folder or launch file is unavailable. If it moved, use Locate folder again in the game’s options.");
            return path;
        }, path -> {
            startPlayer(path, entry.id());
        });
    }

    private void startPlayer(String path) {
        startPlayer(path, null);
    }

    private void startPlayer(String path, String entryId) {
        if (busy) return;
        Intent intent = new Intent(this, MainActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        if (path != null) intent.putExtra("startupPath", path);
        else intent.putExtra("open_browser", true);
        if (entryId != null) intent.putExtra("library_entry_id", entryId);
        launchedGame = true;
        startActivity(intent);
    }

    private interface Work<T> { T run() throws Exception; }
    private interface Done<T> { void run(T value); }

    private <T> void runWork(String message, Work<T> work, Done<T> done) {
        if (busy) return;
        busy = true; busyMessage = message; add.setEnabled(false); progress.setVisibility(View.VISIBLE); status.setText(message);
        worker.execute(() -> {
            T value = null; String error = null;
            try { value = work.run(); } catch (Exception exception) {
                error = exception.getMessage() == null ? "The folder could not be read. Choose it again to restore access." : exception.getMessage();
            }
            final T result = value; final String failure = error;
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) return;
                busy = false; add.setEnabled(true); progress.setVisibility(View.GONE); renderCards();
                if (failure != null) showError("Couldn’t open this game", failure); else done.run(result);
            });
        });
    }

    private void showError(String title, String message) {
        new AlertDialog.Builder(this).setTitle(title).setMessage(message)
                .setPositiveButton("Got it", null).show();
    }

    private void showHelp() {
        new AlertDialog.Builder(this).setTitle("From folder to first scene")
                .setMessage("1. Extract your legally obtained Kirikiri game on your device. Keep its archives, patches and support files in the same folder.\n\n"
                        + "2. Tap Add game folder, select that folder, then allow access. The app finds data.xp3 or startup.tjs automatically. If there are several startup archives, choose the one the game uses.\n\n"
                        + "3. Tap Play. Load saves from the game’s own menu. Use the Controls button for a keyboard, WASD or arrows, touchpad mode and the gesture guide.\n\n"
                        + "Only Kirikiri games are supported. Some games need compatibility patches. Adding a folder does not copy or download a game.")
                .setPositiveButton("Got it", null).show();
    }
}
