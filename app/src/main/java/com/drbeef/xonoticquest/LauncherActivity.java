package com.drbeef.xonoticquest;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

/**
 * Plain launcher activity: makes sure we may read/write /sdcard/XonoticVR, unpacks the
 * default config files on first run and then hands over to the SDL-based engine activity.
 * Kept separate from SDLActivity so the engine thread never starts before storage is usable
 * (same flow as QuakeQuest's GLES3JNIActivity.checkPermissionsAndInitialize()).
 */
public class LauncherActivity extends Activity {
    private static final String TAG = "XonoticVR";
    private static final int PERMISSION_CODE = 16;

    public static final String BASE_DIR = "/sdcard/XonoticVR";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        checkPermissionsAndStart();
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Returning from the "All files access" settings screen
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && Environment.isExternalStorageManager())
            start();
    }

    private void checkPermissionsAndStart() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                intent.setData(Uri.parse("package:" + getPackageName()));
                startActivity(intent);
                return;
            }
        } else {
            String[] perms = {Manifest.permission.WRITE_EXTERNAL_STORAGE, Manifest.permission.READ_EXTERNAL_STORAGE};
            for (String p : perms) {
                if (checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED) {
                    requestPermissions(perms, PERMISSION_CODE);
                    return;
                }
            }
        }
        start();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        if (requestCode == PERMISSION_CODE) {
            for (int r : grantResults)
                if (r != PackageManager.PERMISSION_GRANTED) { finish(); return; }
            start();
        }
    }

    private boolean started = false;

    private void start() {
        if (started) return;
        started = true;

        File root = new File(BASE_DIR);
        File data = new File(root, "data");
        data.mkdirs();
        try { new File(root, ".nomedia").createNewFile(); } catch (Exception ignored) {}
        // a stale session lock (left by a crash) would stop the engine from starting
        new File(root, "lock").delete();
        // a stale pack manifest from older builds would hide pk3s from the engine
        new File(root, "data/ls.txt").delete();

        // Bundled GPL game data (release builds): unpack missing or incomplete pk3s
        copyGameData(root, data);

        // First-run defaults; never overwrite user edits.
        copyAsset("commandline.txt", new File(root, "commandline.txt"));
        copyAsset("vr.cfg", new File(data, "vr.cfg"));
        // menu override: adds Space/Backspace keys to the name editor's character map
        copyAsset("zzz-xonoticvr-menu.pk3", new File(data, "zzz-xonoticvr-menu.pk3"));
        copyAsset("README.txt", new File(root, "README.txt"));

        Intent intent = new Intent(this, XonoticActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        startActivity(intent);
        finish();
    }

    private void copyGameData(File root, File dataDir) {
        try {
            String[] names = getAssets().list("gamedata");
            if (names == null || names.length == 0) return; // engine-only build
            copyAsset("key_0.d0pk", new File(root, "key_0.d0pk"));
            for (String name : names) {
                File to = new File(dataDir, name);
                long want = -1;
                // stored (uncompressed) assets report their exact size; use it to
                // redo copies that were interrupted mid-way
                try (android.content.res.AssetFileDescriptor fd = getAssets().openFd("gamedata/" + name)) {
                    want = fd.getLength();
                } catch (Exception ignored) {}
                if (to.exists() && (want < 0 || to.length() == want)) continue;
                try (InputStream in = getAssets().open("gamedata/" + name); FileOutputStream out = new FileOutputStream(to)) {
                    byte[] buf = new byte[1 << 16];
                    int n;
                    while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
                }
                Log.i(TAG, "Unpacked game data " + to);
            }
        } catch (Exception e) {
            Log.w(TAG, "Could not unpack game data: " + e);
        }
    }

    private void copyAsset(String name, File to) {
        if (to.exists()) return;
        try (InputStream in = getAssets().open(name); FileOutputStream out = new FileOutputStream(to)) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            Log.i(TAG, "Unpacked " + to);
        } catch (Exception e) {
            Log.w(TAG, "Could not unpack asset " + name + ": " + e);
        }
    }
}
