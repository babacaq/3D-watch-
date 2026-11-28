package dev.mobile3d.engine;

import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.io.InputStream;
import java.io.OutputStream;

/**
 * Папка экспорта, выбранная пользователем (системный выбор папки, Storage Access Framework).
 * Если папка не выбрана или запись в неё не удалась, вызывающий код сохраняет в стандартное место (галерея / Загрузки).
 */
final class ExportDir {
    private static final String PREFS = "prismlab_export";
    private static final String KEY = "tree";

    private final Context ctx;
    private volatile Uri tree;
    private volatile String label = "";

    ExportDir(Context ctx) {
        this.ctx = ctx;
        try {
            String s = prefs().getString(KEY, null);
            if (s != null) set(Uri.parse(s));
        } catch (Throwable ignored) { }
    }

    private SharedPreferences prefs() { return ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE); }

    /** Название для экрана настроек; пустая строка: папка не выбрана. */
    String label() { return label; }

    boolean has() { return tree != null; }

    private void set(Uri t) {
        tree = t;
        String l = "";
        try {
            String id = DocumentsContract.getTreeDocumentId(t);  // например primary:Movies/PrismLab
            int c = id.indexOf(':');
            String vol = c > 0 ? id.substring(0, c) : "";
            String path = c >= 0 ? id.substring(c + 1) : id;
            l = ("primary".equals(vol) ? "Память телефона" : (vol.isEmpty() ? "" : "Карта " + vol));
            l = path.isEmpty() ? l : (l.isEmpty() ? path : l + "/" + path);
            if (l.isEmpty()) l = "выбрана";
        } catch (Throwable e) { l = "выбрана"; }
        label = l;
    }

    /** Результат системного выбора папки. */
    void onPicked(Intent data) {
        Uri t = data == null ? null : data.getData();
        if (t == null) return;
        try {
            ctx.getContentResolver().takePersistableUriPermission(t,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        } catch (Throwable e) {
            android.util.Log.w("PrismLab", "persist permission failed", e);
        }
        // отпустить доступ к прежней папке
        Uri old = tree;
        if (old != null && !old.equals(t)) release(old);
        set(t);
        prefs().edit().putString(KEY, t.toString()).apply();
    }

    void clear() {
        Uri old = tree;
        tree = null;
        label = "";
        prefs().edit().remove(KEY).apply();
        if (old != null) release(old);
    }

    private void release(Uri u) {
        try {
            ctx.getContentResolver().releasePersistableUriPermission(u,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        } catch (Throwable ignored) { }
    }

    private OutputStream open(String name, String mime) throws Exception {
        Uri t = tree;
        if (t == null) return null;
        ContentResolver cr = ctx.getContentResolver();
        Uri parent = DocumentsContract.buildDocumentUriUsingTree(t, DocumentsContract.getTreeDocumentId(t));
        Uri doc = DocumentsContract.createDocument(cr, parent, mime, name);
        if (doc == null) return null;
        return cr.openOutputStream(doc);
    }

    /** true: записано в выбранную папку; false: папка не выбрана или не удалось (нужен запасной путь). */
    boolean write(String name, String mime, byte[] data) {
        if (tree == null) return false;
        try (OutputStream os = open(name, mime)) {
            if (os == null) return false;
            os.write(data);
            return true;
        } catch (Throwable e) {
            android.util.Log.e("PrismLab", "write to chosen folder failed", e);
            return false;
        }
    }

    boolean copy(String name, String mime, InputStream in) {
        if (tree == null) return false;
        try (OutputStream os = open(name, mime)) {
            if (os == null) return false;
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
            return true;
        } catch (Throwable e) {
            android.util.Log.e("PrismLab", "copy to chosen folder failed", e);
            return false;
        }
    }
}
