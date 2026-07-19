package dev.phasma.player;

import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.graphics.Insets;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

public class PhasmaPlayerActivity extends org.libsdl.app.SDLActivity {
    private static final String TAG = "PhasmaPlayer";
    private static final String ASSET_ROOT = "Assets";
    // Pre-baked SPIR-V shader cache shipped alongside Assets/. Android has no runtime shader
    // compiler, so the engine can only ReadSpvFile from this cache. It must land at
    // <internalStorage>/ShaderCache/_spv/ (Path::Root), where ShaderCache::Init reads. aapt drops
    // APK asset dirs whose names start with '_', so the blobs ship under ShaderCache/spv and are
    // renamed to the engine's _spv path here on extraction.
    private static final String SHADER_CACHE_ASSET = "ShaderCache/spv";
    private static final String SHADER_CACHE_DEST = "ShaderCache/_spv";
    private static final String VERSION_MARKER = ".assets_version";
    private static volatile int sSafeAreaInsetLeft = 0;
    private static volatile int sSafeAreaInsetTop = 0;
    private static volatile int sSafeAreaInsetRight = 0;
    private static volatile int sSafeAreaInsetBottom = 0;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        extractBundledAssets();
        super.onCreate(savedInstanceState);
        hideSystemBars();
        installSafeAreaInsetsListener();
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
    }

    public static int getSafeAreaInsetLeft() {
        return sSafeAreaInsetLeft;
    }

    public static int getSafeAreaInsetTop() {
        return sSafeAreaInsetTop;
    }

    public static int getSafeAreaInsetRight() {
        return sSafeAreaInsetRight;
    }

    public static int getSafeAreaInsetBottom() {
        return sSafeAreaInsetBottom;
    }

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
            return;
        }

        getWindow()
                .getDecorView()
                .setSystemUiVisibility(
                        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                                | View.SYSTEM_UI_FLAG_FULLSCREEN
                                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    // Extract the packaged Assets/ tree into app-private storage so the native engine's
    // Path::Assets (SDL_AndroidGetInternalStoragePath()/Assets/) resolves on device.
    // Re-extract only when the installed APK changes: this avoids re-paying the full
    // copy on every cold start while still refreshing assets during same-version debug reinstalls.
    private void extractBundledAssets() {
        File assetsDir = new File(getFilesDir(), ASSET_ROOT);
        File shaderCacheDir = new File(getFilesDir(), SHADER_CACHE_DEST);
        File marker = new File(getFilesDir(), VERSION_MARKER);
        String version = appAssetVersion();
        // Also require the shader cache to be present, so updating over an install that predates
        // the pre-baked cache (same versionCode, marker already written) still extracts it.
        if (assetsDir.isDirectory() && shaderCacheDir.isDirectory() && version.equals(readMarker(marker))) {
            return;
        }
        try {
            copyAssetTree(getAssets(), ASSET_ROOT, assetsDir);
            deleteTree(shaderCacheDir);
            // Pre-baked shader cache is optional in the APK (absent until a bake is staged);
            // copyAssetTree just mkdirs an empty dir if no ShaderCache assets are bundled.
            copyAssetTree(getAssets(), SHADER_CACHE_ASSET, new File(getFilesDir(), SHADER_CACHE_DEST));
            writeMarker(marker, version);
        } catch (IOException e) {
            Log.e(TAG, "Failed to extract bundled assets", e);
        }
    }

    private void installSafeAreaInsetsListener() {
        View decor = getWindow().getDecorView();
        decor.setOnApplyWindowInsetsListener((view, insets) -> {
            updateSafeAreaInsets(insets);
            return insets;
        });
        decor.post(() -> updateSafeAreaInsets(decor.getRootWindowInsets()));
    }

    private static void updateSafeAreaInsets(WindowInsets insets) {
        if (insets == null) {
            return;
        }

        if (Build.VERSION.SDK_INT >= 30) {
            Insets systemBars =
                    insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            sSafeAreaInsetLeft = systemBars.left;
            sSafeAreaInsetTop = systemBars.top;
            sSafeAreaInsetRight = systemBars.right;
            sSafeAreaInsetBottom = systemBars.bottom;
        } else {
            sSafeAreaInsetLeft = insets.getSystemWindowInsetLeft();
            sSafeAreaInsetTop = insets.getSystemWindowInsetTop();
            sSafeAreaInsetRight = insets.getSystemWindowInsetRight();
            sSafeAreaInsetBottom = insets.getSystemWindowInsetBottom();
        }
    }

    @SuppressWarnings("deprecation")
    private String appAssetVersion() {
        try {
            android.content.pm.PackageInfo packageInfo =
                    getPackageManager().getPackageInfo(getPackageName(), 0);
            return packageInfo.versionCode + ":" + packageInfo.lastUpdateTime;
        } catch (PackageManager.NameNotFoundException e) {
            return "-1";
        }
    }

    private static String readMarker(File marker) {
        if (!marker.isFile()) {
            return null;
        }
        try (InputStream input = new FileInputStream(marker)) {
            byte[] buffer = new byte[32];
            int read = input.read(buffer);
            return read > 0 ? new String(buffer, 0, read, StandardCharsets.UTF_8).trim() : null;
        } catch (IOException e) {
            return null;
        }
    }

    private static void writeMarker(File marker, String version) {
        try (FileOutputStream output = new FileOutputStream(marker)) {
            output.write(version.getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            Log.w(TAG, "Failed to write asset version marker", e);
        }
    }

    private static void deleteTree(File file) throws IOException {
        if (!file.exists()) {
            return;
        }
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteTree(child);
                }
            }
        }
        if (!file.delete()) {
            throw new IOException("Failed to delete: " + file);
        }
    }

    private static void copyAssetTree(AssetManager assets, String assetPath, File destination)
            throws IOException {
        String[] children = assets.list(assetPath);
        if (children == null || children.length == 0) {
            // AssetManager.list() returns an empty array for BOTH a leaf file and an empty
            // directory, so disambiguate by attempting to open the path as a stream: open
            // succeeds for a file and throws for a directory.
            InputStream input;
            try {
                input = assets.open(assetPath);
            } catch (IOException notAFile) {
                if (!destination.isDirectory() && !destination.mkdirs()) {
                    throw new IOException("Failed to create directory: " + destination);
                }
                return;
            }
            copyAssetStream(input, destination);
            return;
        }

        if (!destination.isDirectory() && !destination.mkdirs()) {
            throw new IOException("Failed to create directory: " + destination);
        }

        for (String child : children) {
            copyAssetTree(assets, assetPath + "/" + child, new File(destination, child));
        }
    }

    private static void copyAssetStream(InputStream input, File destination) throws IOException {
        File parent = destination.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("Failed to create directory: " + parent);
        }

        try (InputStream in = input;
             FileOutputStream output = new FileOutputStream(destination)) {
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = in.read(buffer)) != -1) {
                output.write(buffer, 0, read);
            }
        }
    }
}
