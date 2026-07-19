import java.util.Properties

plugins {
    id("com.android.application")
}

val engineRoot = rootProject.layout.projectDirectory.dir("../../..").asFile.canonicalFile
val localProperties = Properties().apply {
    val file = rootProject.file("local.properties")
    if (file.isFile) {
        file.inputStream().use { load(it) }
    }
}

fun localProperty(name: String): String? =
    localProperties.getProperty(name)
        ?: providers.gradleProperty(name).orNull
        ?: System.getenv(name)

val sdl2RootPath = localProperty("sdl2Root") ?: localProperty("SDL2_ANDROID_ROOT") ?: ""
if (sdl2RootPath.isBlank()) {
    logger.warn("Set sdl2Root in local.properties to a full SDL2 source tree before building Android.")
}
val spirvCrossRootPath = localProperty("spirvCrossRoot") ?: localProperty("SPIRV_CROSS_ROOT") ?: ""
if (spirvCrossRootPath.isBlank()) {
    logger.warn("Set spirvCrossRoot in local.properties to a full SPIRV-Cross source tree before building Android.")
}
val sdl2Root = if (sdl2RootPath.isBlank()) {
    rootProject.file("__missing_sdl2__")
} else {
    file(sdl2RootPath).canonicalFile
}
val spirvCrossRoot = if (spirvCrossRootPath.isBlank()) {
    rootProject.file("__missing_spirv_cross__")
} else {
    file(spirvCrossRootPath).canonicalFile
}
val androidAbiFilters = (localProperty("androidAbiFilters") ?: "arm64-v8a")
    .split(',')
    .map { it.trim() }
    .filter { it.isNotEmpty() }

// Pre-baked SPIR-V shader cache (produced by tools/bake_android_shaders.ps1). Android builds with
// no runtime shader compiler, so these vulkan1.2-keyed blobs are the only way the device can create
// shaders. The engine reads them from `ShaderCache/_spv/<key>` (Path::Root). IMPORTANT: aapt's
// default asset-ignore pattern drops directories whose names start with `_` (the `<dir>_*` rule), so
// the underscore dir cannot ship under that name. We stage the blobs as `ShaderCache/spv/` (no
// underscore) and PhasmaPlayerActivity renames `spv` -> `_spv` when extracting to internal storage.
val prebakedSpvDir = rootProject.layout.projectDirectory.dir("prebaked/ShaderCache/_spv")

// Cooked models (".pemesh" + their textures), produced by tools/cook_model.py into the gitignored
// prebaked staging. Cooked assets are build artifacts, not source, so they never live under
// Phasma/Editor/EditorAssets; they are staged into the APK at Assets/Models/<name>/ where the player loads
// them (the player loads only .pemesh; Assimp/import is editor-only).
val prebakedModelsDir = rootProject.layout.projectDirectory.dir("prebaked/Models")

// A single Sync owns the whole generated assets tree (engine assets under Assets/, the pre-baked
// shader cache under ShaderCache/spv/). One task with one output dir is what the AGP asset merger
// reliably tracks — a second srcDir or a separate Sync into a subdir was silently dropped by the
// up-to-date merge. Scenes/ is excluded: the only loadable Android scene is the tracked
// src/main/assets/Assets/Scenes/new.pescene (Assimp is off, so model scenes can't load), and
// staging the working-tree Scenes/ would bundle whatever untracked local .pescene files happen to
// exist, making the APK machine-dependent.
val stagedAssetsDir = layout.buildDirectory.dir("generated/phasmaAssets")
val stagePhasmaAssets by tasks.registering(Sync::class) {
    // Engine runtime assets (shaders, objects, particles, fonts, PassInfo) live in
    // Phasma/Runtime/RuntimeAssets after the 2026-06-13 RuntimeAssets carve;
    // Phasma/Editor/EditorAssets is editor chrome only. Shader SOURCES must ship because
    // Shader::Create checks the .hlsl exists before the prebaked cache lookup.
    from(engineRoot.resolve("Phasma/Runtime/RuntimeAssets")) {
        into("Assets")
        exclude(
            "Objects/glTF-Sample-Models/**",
            "Objects/main_sponza/**",
            "Scenes/**"
        )
    }
    from(prebakedSpvDir) {
        into("ShaderCache/spv")
    }
    from(prebakedModelsDir) {
        into("Assets/Models")
    }
    // AgainstTheHero game project (sibling repo): stage the complete runtime asset tree.
    // Old archived modes and the desktop-only entry script stay excluded.
    val athAssets = engineRoot.resolve("../AgainstTheHero/Assets")
    if (athAssets.isDirectory) {
        from(athAssets) {
            into("Assets")
            exclude("Scripts/old/**", "Scripts/Player/against_the_hero.lua")
        }
    } else {
        logger.warn("AgainstTheHero project not found at $athAssets — APK will lack the game.")
    }
    duplicatesStrategy = DuplicatesStrategy.EXCLUDE
    into(stagedAssetsDir)
    // Hard-fail (at task time, not configuration time so `gradlew tasks`/IDE sync still work):
    // without a baked cache the APK is guaranteed to abort at the first Shader::Create on device.
    doFirst {
        val count = prebakedSpvDir.asFile.listFiles()?.size ?: 0
        if (count == 0) {
            throw GradleException(
                "No pre-baked shaders in ${prebakedSpvDir.asFile} — run tools/bake_android_shaders.ps1 " +
                    "before assembling, or the APK will fail at Shader::Create on device."
            )
        }
        logger.lifecycle("Bundling $count pre-baked SPIR-V shader(s) into the APK.")
    }
}

android {
    namespace = "dev.phasma.player"
    compileSdk = 35
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "dev.phasma.player"
        minSdk = 26
        targetSdk = 35
        versionCode = 20
        versionName = "0.20"

        ndk {
            abiFilters += androidAbiFilters
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DPE_ANDROID_SDL2_ROOT=${sdl2Root.invariantSeparatorsPath}",
                    "-DPE_ANDROID_SPIRV_CROSS_ROOT=${spirvCrossRoot.invariantSeparatorsPath}",
                    "-DPE_BUILD_EDITOR=OFF",
                    "-DPE_BUILD_LAUNCHER=OFF",
                    "-DPE_BUILD_MCP=OFF",
                    "-DPE_BUILD_PLAYER=ON",
                    "-DPE_WEBGPU=OFF",
                    "-DPE_ENABLE_ASSIMP=OFF",
                    "-DPE_ENABLE_RUNTIME_SHADER_COMPILER=OFF",
                    "-DPE_PHYSICS=ON",
                    "-DPE_AUDIO=OFF",
                    "-DPE_TRACY=OFF"
                )
                targets += "PhasmaPlayer"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = engineRoot.resolve("CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }

    sourceSets.getByName("main") {
        assets.srcDir(stagedAssetsDir)
        java.srcDir("src/main/java")
        if (sdl2RootPath.isNotBlank()) {
            java.srcDir(sdl2Root.resolve("android-project/app/src/main/java"))
        }
    }
}

tasks.configureEach {
    if ((name.startsWith("merge") && name.endsWith("Assets")) ||
        name.contains("lint", ignoreCase = true)) {
        dependsOn(stagePhasmaAssets)
    }
}
