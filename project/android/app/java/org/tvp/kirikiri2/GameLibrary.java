package org.tvp.kirikiri2;

import android.content.Context;
import android.content.SharedPreferences;
import android.net.Uri;
import androidx.documentfile.provider.DocumentFile;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;

/** Remembers references to games. Removing an entry never touches game files. */
public final class GameLibrary {
    public static final class Entry {
        public String tree;
        public String folder;
        public String name;
        public String launchFile;
        public boolean favorite;
        public long lastPlayed;

        public Entry(String tree, String folder, String name, String launchFile) {
            this.tree = tree;
            this.folder = folder;
            this.name = name;
            this.launchFile = launchFile;
        }

        public String id() { return tree + "\n" + folder; }

        public String path(Context context) throws IOException {
            return StorageAccess.pathForTree(context, Uri.parse(tree))
                    + (folder.isEmpty() ? "" : "/" + folder)
                    + (launchFile.isEmpty() ? "" : "/" + launchFile);
        }

        public DocumentFile directory(Context context) {
            DocumentFile root = DocumentFile.fromTreeUri(context, Uri.parse(tree));
            if (root == null) return null;
            for (String part : folder.split("/")) {
                if (!part.isEmpty()) root = root == null ? null : root.findFile(part);
            }
            return root;
        }
    }

    public static final class Folder {
        public final Entry entry;
        public final List<String> candidates;
        Folder(Entry entry, List<String> candidates) {
            this.entry = entry;
            this.candidates = candidates;
        }
        public boolean needsChoice() { return entry.launchFile == null; }
    }

    private final SharedPreferences preferences;

    public GameLibrary(Context context) {
        preferences = context.getSharedPreferences("game_library", Context.MODE_PRIVATE);
    }

    public List<Entry> load() {
        List<Entry> entries = new ArrayList<>();
        try {
            JSONArray array = new JSONArray(preferences.getString("entries", "[]"));
            for (int i = 0; i < array.length(); ++i) {
                JSONObject item = array.getJSONObject(i);
                Entry entry = new Entry(item.getString("tree"), item.optString("folder"),
                        item.getString("name"), item.getString("launch"));
                entry.favorite = item.optBoolean("favorite");
                entry.lastPlayed = item.optLong("played");
                entries.add(entry);
            }
        } catch (JSONException ignored) { }
        return entries;
    }

    private void save(List<Entry> entries) {
        JSONArray array = new JSONArray();
        try {
            for (Entry entry : entries) {
                JSONObject item = new JSONObject();
                item.put("tree", entry.tree);
                item.put("folder", entry.folder);
                item.put("name", entry.name);
                item.put("launch", entry.launchFile);
                item.put("favorite", entry.favorite);
                item.put("played", entry.lastPlayed);
                array.put(item);
            }
        } catch (JSONException impossible) { throw new IllegalStateException(impossible); }
        // Entries are tiny; commit before launching the separate engine process.
        preferences.edit().putString("entries", array.toString()).commit();
    }

    public void put(Entry entry) {
        List<Entry> entries = load();
        for (int i = 0; i < entries.size(); ++i) {
            if (entries.get(i).id().equals(entry.id())) {
                entries.set(i, entry);
                save(entries);
                return;
            }
        }
        entries.add(entry);
        save(entries);
    }

    public void remove(Entry entry) {
        List<Entry> entries = load();
        for (int i = entries.size() - 1; i >= 0; --i)
            if (entries.get(i).id().equals(entry.id())) entries.remove(i);
        save(entries);
    }

    public static List<Entry> filter(List<Entry> entries, String query, int order) {
        List<Entry> result = new ArrayList<>();
        String needle = query.trim().toLowerCase(Locale.ROOT);
        for (Entry entry : entries)
            if (entry.name.toLowerCase(Locale.ROOT).contains(needle) && (order != 2 || entry.favorite))
                result.add(entry);
        Collections.sort(result, new Comparator<Entry>() {
            @Override public int compare(Entry a, Entry b) {
                if (order != 1 && a.favorite != b.favorite) return a.favorite ? -1 : 1;
                if (order != 1 && a.lastPlayed != b.lastPlayed) return Long.compare(b.lastPlayed, a.lastPlayed);
                return a.name.compareToIgnoreCase(b.name);
            }
        });
        return result;
    }

    private static Folder inspect(Uri tree, DocumentFile directory, String relative) throws IOException {
        List<String> candidates = new ArrayList<>();
        boolean startup = false;
        String data = null;
        for (DocumentFile file : directory.listFiles()) {
            if (!file.isFile() || file.getName() == null) continue;
            String name = file.getName();
            String lower = name.toLowerCase(Locale.ROOT);
            if (lower.equals("startup.tjs")) startup = true;
            if (lower.endsWith(".xp3") || lower.endsWith(".exe")) candidates.add(name);
            if (lower.equals("data.xp3")) data = name;
        }
        if (!startup && candidates.isEmpty()) return null;
        Collections.sort(candidates, String.CASE_INSENSITIVE_ORDER);
        String launch = startup ? "" : data != null ? data : candidates.size() == 1 ? candidates.get(0) : null;
        return new Folder(new Entry(tree.toString(), relative,
                directory.getName() == null ? "Untitled game" : directory.getName(), launch), candidates);
    }

    /** Scan only the selected folder and its immediate children, never a whole drive. */
    public static List<Folder> discover(Context context, Uri tree) throws IOException {
        DocumentFile root = DocumentFile.fromTreeUri(context, tree);
        if (root == null || !root.isDirectory() || !root.canRead())
            throw new IOException("This folder is unavailable. Choose it again to restore access.");
        List<Folder> result = new ArrayList<>();
        Folder direct = inspect(tree, root, "");
        if (direct != null) { result.add(direct); return result; }
        int visited = 0;
        for (DocumentFile child : root.listFiles()) {
            if (!child.isDirectory() || child.getName() == null) continue;
            if (++visited > 100) break;
            Folder found = inspect(tree, child, child.getName());
            if (found != null) result.add(found);
        }
        if (result.isEmpty()) throw new IOException("No game found here. Select the extracted game folder containing data.xp3 or startup.tjs. ZIP and RAR files need to be extracted first.");
        return result;
    }

    public static List<String> launchFiles(Context context, Entry entry) throws IOException {
        DocumentFile directory = entry.directory(context);
        if (directory == null || !directory.canRead()) throw new IOException("Choose this folder again to restore access.");
        Folder folder = inspect(Uri.parse(entry.tree), directory, entry.folder);
        if (folder == null) throw new IOException("No startup.tjs, XP3 archive or game executable was found here.");
        List<String> result = new ArrayList<>(folder.candidates);
        if ("".equals(folder.entry.launchFile)) result.add(0, "");
        return result;
    }
}
