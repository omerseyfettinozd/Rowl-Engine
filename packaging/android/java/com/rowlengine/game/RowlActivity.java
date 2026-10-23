package com.rowlengine.game;

import android.content.res.AssetManager;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import org.libsdl.app.SDLActivity;

/**
 * Rowl Engine Android host. The packaged game ships as APK assets under
 * "game/"; the native player reads plain files, so the game is copied to
 * the app's private storage once per installed APK version and the player is
 * started with "--project <that dir>". Saves and the player profile go to
 * XDG_DATA_HOME = <files>/data (per-game dirs, D01b).
 */
public class RowlActivity extends SDLActivity {
    private static final String TAG = "RowlEngine";
    private static final String GAME_ASSET_DIR = "game";

    private File gameDir;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        gameDir = new File(getFilesDir(), GAME_ASSET_DIR);
        File dataDir = new File(getFilesDir(), "data");
        //noinspection ResultOfMethodCallIgnored
        dataDir.mkdirs();
        try {
            Os.setenv("XDG_DATA_HOME", dataDir.getAbsolutePath(), true);
            Os.setenv("HOME", getFilesDir().getAbsolutePath(), true);
            // SDL picks the orientation from this hint (the manifest alone is overridden).
            Os.setenv("SDL_ORIENTATIONS", "LandscapeLeft LandscapeRight", true);
        } catch (ErrnoException e) {
            Log.e(TAG, "setenv failed", e);
        }
        try {
            installGameIfNeeded();
        } catch (IOException e) {
            Log.e(TAG, "Game install from APK assets failed", e);
        }
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "RowlEngineCore", "main"};
    }

    @Override
    protected String[] getArguments() {
        return new String[] {"--project", gameDir.getAbsolutePath()};
    }

    private void installGameIfNeeded() throws IOException {
        long apkStamp;
        try {
            apkStamp = getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime;
        } catch (Exception e) {
            apkStamp = System.currentTimeMillis();
        }
        File marker = new File(getFilesDir(), "game.installed");
        String expected = Long.toString(apkStamp);
        if (gameDir.isDirectory() && marker.isFile() && expected.equals(readSmall(marker))) {
            return;
        }
        deleteTree(gameDir);
        copyAssetTree(getAssets(), GAME_ASSET_DIR, gameDir);
        try (OutputStream out = new FileOutputStream(marker)) {
            out.write(expected.getBytes("UTF-8"));
        }
        Log.i(TAG, "Game installed to " + gameDir);
    }

    private static void copyAssetTree(AssetManager assets, String path, File target) throws IOException {
        String[] children = assets.list(path);
        if (children == null || children.length == 0) {
            //noinspection ResultOfMethodCallIgnored
            target.getParentFile().mkdirs();
            try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(target)) {
                byte[] buffer = new byte[64 * 1024];
                int read;
                while ((read = in.read(buffer)) > 0) {
                    out.write(buffer, 0, read);
                }
            }
            return;
        }
        //noinspection ResultOfMethodCallIgnored
        target.mkdirs();
        for (String child : children) {
            copyAssetTree(assets, path + "/" + child, new File(target, child));
        }
    }

    private static String readSmall(File file) {
        try (InputStream in = new java.io.FileInputStream(file)) {
            byte[] data = new byte[64];
            int read = in.read(data);
            return read > 0 ? new String(data, 0, read, "UTF-8") : "";
        } catch (IOException e) {
            return "";
        }
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        //noinspection ResultOfMethodCallIgnored
        file.delete();
    }
}
