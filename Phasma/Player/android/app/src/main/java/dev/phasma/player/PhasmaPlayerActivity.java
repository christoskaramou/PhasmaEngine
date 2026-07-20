package dev.phasma.player;

import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.graphics.Insets;
import android.os.Build;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
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
import java.util.ArrayList;
import java.util.List;

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

    // Recents redelivers the last launch intent, so adb-harness extras (profiler,
    // arena autostart, god mode) would leak into a normal user relaunch without
    // this guard.
    private boolean isHistoryRelaunch() {
        return getIntent() != null
                && (getIntent().getFlags() & android.content.Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY) != 0;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        if (!isHistoryRelaunch()) {
            applyProfilerLaunchOptions();
            applyAthLaunchEnv();
        } else {
            Log.i(TAG, "Relaunch from Recents: ignoring redelivered launch extras");
        }
        extractBundledAssets();
        super.onCreate(savedInstanceState);
        hideSystemBars();
        installSafeAreaInsetsListener();
        requestMaxRefreshRate();
    }

    // Without an explicit mode request Android keeps many panels at 60/90Hz, which
    // caps FIFO presentation regardless of how fast the engine renders. Ask for the
    // highest refresh rate available at the current resolution (e.g. 120Hz).
    private void requestMaxRefreshRate() {
        try {
            android.view.Display display = Build.VERSION.SDK_INT >= 30
                    ? getDisplay()
                    : getWindowManager().getDefaultDisplay();
            if (display == null) {
                return;
            }
            android.view.Display.Mode current = display.getMode();
            android.view.Display.Mode best = current;
            for (android.view.Display.Mode mode : display.getSupportedModes()) {
                if (mode.getPhysicalWidth() == current.getPhysicalWidth()
                        && mode.getPhysicalHeight() == current.getPhysicalHeight()
                        && mode.getRefreshRate() > best.getRefreshRate()) {
                    best = mode;
                }
            }
            android.view.WindowManager.LayoutParams params = getWindow().getAttributes();
            params.preferredDisplayModeId = best.getModeId();
            getWindow().setAttributes(params);
            Log.i(TAG, "Requested display mode " + best.getRefreshRate() + "Hz (id " + best.getModeId() + ")");
        } catch (Throwable t) {
            Log.w(TAG, "Failed to request high refresh rate", t);
        }
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

    // Opt-in live profiler: adb shell am start -n dev.phasma.player/.PhasmaPlayerActivity
    //   --ez PE_PROFILER true   OR   --es PE_PROFILER 9876
    // Then: adb forward tcp:9876 tcp:9876 and connect desktop PhasmaProfiler.
    @Override
    protected String[] getArguments() {
        List<String> args = new ArrayList<>();
        Bundle extras = (getIntent() != null && !isHistoryRelaunch()) ? getIntent().getExtras() : null;
        if (extras != null && extras.containsKey("PE_PROFILER")) {
            Object value = extras.get("PE_PROFILER");
            boolean enabled = true;
            String port = null;
            if (value instanceof Boolean) {
                enabled = (Boolean) value;
            } else if (value instanceof Number) {
                port = Integer.toString(((Number) value).intValue());
            } else if (value != null) {
                String text = String.valueOf(value).trim();
                if (text.isEmpty() || text.equals("0") || text.equalsIgnoreCase("false")) {
                    enabled = false;
                } else if (!text.equals("1") && !text.equalsIgnoreCase("true")) {
                    port = text;
                }
            }
            if (enabled) {
                if (port != null) {
                    args.add("--profiler=" + port);
                } else {
                    args.add("--profiler");
                }
            }
        }
        return args.toArray(new String[0]);
    }

    private void applyProfilerLaunchOptions() {
        Bundle extras = getIntent() != null ? getIntent().getExtras() : null;
        if (extras == null || !extras.containsKey("PE_PROFILER")) {
            return;
        }
        Object value = extras.get("PE_PROFILER");
        // ParseProfilerStreamPortArg treats a leading digit as a port. Never set
        // PE_PROFILER=1 (that becomes port 1). Use "true" or an explicit port.
        String envValue = "true";
        if (value instanceof Boolean) {
            if (!(Boolean) value) {
                return;
            }
        } else if (value instanceof Number) {
            int port = ((Number) value).intValue();
            if (port <= 0) {
                return;
            }
            envValue = Integer.toString(port);
        } else if (value != null) {
            String text = String.valueOf(value).trim();
            if (text.isEmpty() || text.equals("0") || text.equalsIgnoreCase("false")) {
                return;
            }
            if (!text.equals("1") && !text.equalsIgnoreCase("true")) {
                envValue = text;
            }
        }
        try {
            Os.setenv("PE_PROFILER", envValue, true);
            Log.i(TAG, "PE_PROFILER=" + envValue);
        } catch (ErrnoException e) {
            Log.w(TAG, "Failed to set PE_PROFILER env", e);
        }
    }

    // ATH launch env from intent extras (adb --es / --ez):
    //   ATH_MONO_ARCH=sprout|off
    //   ATH_DUEL_INVULNERABLE=true  (god mode for perf captures)
    private void applyAthLaunchEnv() {
        Bundle extras = getIntent() != null ? getIntent().getExtras() : null;
        if (extras == null) {
            return;
        }
        setAthEnvFromExtra(extras, "ATH_MONO_ARCH");
        setAthEnvFromExtra(extras, "ATH_DUEL_INVULNERABLE");
        // Direct arena boot + self-starting combat for hands-off perf captures:
        //   --es ATH_MODE arena --ez ATH_AUTOSTART true
        setAthEnvFromExtra(extras, "ATH_MODE");
        setAthEnvFromExtra(extras, "ATH_AUTOSTART");
        // Perf map/wave pins (Duel:start_map): --ei ATH_DUEL_MAP 8 --ei ATH_DUEL_WAVE 1
        setAthEnvFromExtra(extras, "ATH_DUEL_MAP");
        setAthEnvFromExtra(extras, "ATH_DUEL_WAVE");
        // Present-mode A/B knob: --es PE_PRESENT_MODE mailbox|immediate|fifo
        // (read by RuntimeStartup's ReadPresentModeEnvOverride; default stays FIFO).
        setAthEnvFromExtra(extras, "PE_PRESENT_MODE");
    }

    private void setAthEnvFromExtra(Bundle extras, String key) {
        if (!extras.containsKey(key)) {
            return;
        }
        Object value = extras.get(key);
        String text;
        if (value instanceof Boolean) {
            text = (Boolean) value ? "1" : "0";
        } else if (value == null) {
            return;
        } else {
            text = String.valueOf(value).trim();
            if (text.isEmpty()) {
                return;
            }
            if (text.equalsIgnoreCase("true")) {
                text = "1";
            } else if (text.equalsIgnoreCase("false")) {
                text = "0";
            }
        }
        try {
            Os.setenv(key, text, true);
            Log.i(TAG, key + "=" + text);
        } catch (ErrnoException e) {
            Log.w(TAG, "Failed to set " + key + " env", e);
        }
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
