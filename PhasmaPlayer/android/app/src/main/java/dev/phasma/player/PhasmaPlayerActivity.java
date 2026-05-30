package dev.phasma.player;

import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;
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

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        extractBundledAssets();
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
    }

    // Extract the packaged Assets/ tree into app-private storage so the native engine's
    // Path::Assets (SDL_AndroidGetInternalStoragePath()/Assets/) resolves on device.
    // Re-extract only when the app versionCode changes: this avoids re-paying the full
    // copy on every cold start and avoids clobbering files a game writes under Assets/.
    private void extractBundledAssets() {
        File assetsDir = new File(getFilesDir(), ASSET_ROOT);
        File shaderCacheDir = new File(getFilesDir(), SHADER_CACHE_DEST);
        File marker = new File(getFilesDir(), VERSION_MARKER);
        String version = Integer.toString(appVersionCode());
        // Also require the shader cache to be present, so updating over an install that predates
        // the pre-baked cache (same versionCode, marker already written) still extracts it.
        if (assetsDir.isDirectory() && shaderCacheDir.isDirectory() && version.equals(readMarker(marker))) {
            return;
        }
        try {
            copyAssetTree(getAssets(), ASSET_ROOT, assetsDir);
            // Pre-baked shader cache is optional in the APK (absent until a bake is staged);
            // copyAssetTree just mkdirs an empty dir if no ShaderCache assets are bundled.
            copyAssetTree(getAssets(), SHADER_CACHE_ASSET, new File(getFilesDir(), SHADER_CACHE_DEST));
            writeMarker(marker, version);
        } catch (IOException e) {
            Log.e(TAG, "Failed to extract bundled assets", e);
        }
    }

    @SuppressWarnings("deprecation")
    private int appVersionCode() {
        try {
            return getPackageManager().getPackageInfo(getPackageName(), 0).versionCode;
        } catch (PackageManager.NameNotFoundException e) {
            return -1;
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
