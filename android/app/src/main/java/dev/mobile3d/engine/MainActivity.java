package dev.mobile3d.engine;

import android.app.NativeActivity;
import android.content.ClipData;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.View;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * Окно целиком рисует нативный код (C++/OpenGL ES). Java нужна для двух вещей:
 * выбор файлов через системный диалог (OBJ+MTL+текстуры, GLTF+BIN, GLB, STL) и открытие моделей через «Открыть с помощью».
 * Выбранные файлы копируются в папку приложения, а нативный код забирает их пути через takePending().
 */
public class MainActivity extends NativeActivity {
    private static final int REQ_PICK = 1001;
    private final List<String> pending = new ArrayList<>();

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        handleIntent(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        handleIntent(intent);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }

    /** Вызывается из C++ (кнопка OPEN). */
    public void openPicker() {
        runOnUiThread(() -> {
            try {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");
                i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
                startActivityForResult(i, REQ_PICK);
            } catch (Exception e) {
                Toast.makeText(this, "File picker unavailable", Toast.LENGTH_LONG).show();
            }
        });
    }

    /** Вызывается из C++ несколько раз в секунду. Возвращает пути скопированных файлов через \n или null. */
    public synchronized String takePending() {
        if (pending.isEmpty()) return null;
        StringBuilder sb = new StringBuilder();
        for (String p : pending) sb.append(p).append('\n');
        pending.clear();
        return sb.toString();
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_PICK || res != RESULT_OK || data == null) return;
        List<Uri> uris = new ArrayList<>();
        ClipData cd = data.getClipData();
        if (cd != null) for (int i = 0; i < cd.getItemCount(); i++) uris.add(cd.getItemAt(i).getUri());
        else if (data.getData() != null) uris.add(data.getData());
        importUris(uris);
    }

    private void handleIntent(Intent intent) {
        if (intent == null || !Intent.ACTION_VIEW.equals(intent.getAction()) || intent.getData() == null) return;
        List<Uri> uris = new ArrayList<>();
        uris.add(intent.getData());
        importUris(uris);
    }

    private void importUris(final List<Uri> uris) {
        new Thread(() -> {
            File base = getExternalFilesDir(null);
            if (base == null) base = getFilesDir();
            File dir = new File(base, "models/" + System.currentTimeMillis());
            if (!dir.mkdirs() && !dir.isDirectory()) return;
            List<String> done = new ArrayList<>();
            for (Uri u : uris) {
                String name = displayName(u);
                File out = new File(dir, name);
                try (InputStream in = getContentResolver().openInputStream(u); OutputStream os = new FileOutputStream(out)) {
                    if (in == null) continue;
                    byte[] buf = new byte[1 << 16];
                    int n;
                    while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
                    done.add(out.getAbsolutePath());
                } catch (Exception e) {
                    runOnUiThread(() -> Toast.makeText(this, "Cannot read file: " + e.getMessage(), Toast.LENGTH_LONG).show());
                }
            }
            synchronized (this) {
                // модельные файлы идут первыми: остальное (mtl, bin, текстуры) нативный код найдёт рядом по имени
                pending.addAll(done);
            }
        }, "import").start();
    }

    private String displayName(Uri u) {
        String name = null;
        if ("content".equals(u.getScheme())) {
            try (Cursor c = getContentResolver().query(u, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                if (c != null && c.moveToFirst()) name = c.getString(0);
            } catch (Exception ignored) { }
        }
        if (name == null) name = u.getLastPathSegment();
        if (name == null) name = "model.bin";
        name = name.replace('/', '_').replace('\\', '_');
        return name;
    }
}
