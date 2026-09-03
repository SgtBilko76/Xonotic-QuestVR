package com.drbeef.xonoticquest;

import android.content.pm.ActivityInfo;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileInputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Scanner;

/**
 * SDL host for the DarkPlaces engine (libxonotic.so). The engine's SDL_main() runs on SDL's
 * thread; the OpenXR layer inside the engine takes the EGL context SDL created.
 */
public class XonoticActivity extends SDLActivity {
    private static final String TAG = "XonoticVR";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        super.onCreate(savedInstanceState);
        // Let the VR layer know which runtime vendor we are on (Quest/Pico/...).
        nativeSetenv("xr_manufacturer", Build.MANUFACTURER.toUpperCase());
        nativeSetenv("XONOTICVR_DIR", LauncherActivity.BASE_DIR);
    }

    @Override
    protected String[] getLibraries() {
        // openxr_loader must be resident before the engine dlopen()s it by name.
        return new String[]{"openxr_loader", "SDL2", "xonotic"};
    }

    @Override
    protected String getMainSharedObject() {
        return getContext().getApplicationInfo().nativeLibraryDir + "/libxonotic.so";
    }

    @Override
    protected String getMainFunction() {
        return "SDL_main";
    }

    @Override
    protected String[] getArguments() {
        List<String> args = new ArrayList<>();
        File cmdline = new File(LauncherActivity.BASE_DIR, "commandline.txt");
        try (FileInputStream fis = new FileInputStream(cmdline); Scanner sc = new Scanner(fis)) {
            while (sc.hasNextLine()) {
                String line = sc.nextLine().trim();
                if (line.isEmpty() || line.startsWith("//") || line.startsWith("#")) continue;
                for (String tok : line.split("\\s+"))
                    if (!tok.isEmpty()) args.add(tok);
            }
        } catch (Exception e) {
            Log.w(TAG, "commandline.txt unreadable, using defaults: " + e);
        }
        if (args.isEmpty()) {
            args.add("-xonotic");
            args.add("-basedir"); args.add(LauncherActivity.BASE_DIR);
            args.add("-nohome");
            args.add("+exec"); args.add("vr.cfg");
        }
        Log.i(TAG, "Engine args: " + args);
        return args.toArray(new String[0]);
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (SDLActivity.mBrokenLibraries) return false;
        return getWindow().superDispatchKeyEvent(event);
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        // The engine keeps global state; a clean process restart is the safest way back.
        System.exit(0);
    }
}
