plugins {
    id("com.android.application")
}

android {
    namespace = "com.drbeef.xonoticquest"
    ndkVersion = "27.2.12479018"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.sgtbilko.xonoticquest"
        minSdk = 26
        targetSdk = 32
        versionCode = 9
        versionName = "0.3.1"

        externalNativeBuild {
            cmake {
                abiFilters("arm64-v8a")
                arguments("-DANDROID_USE_LEGACY_TOOLCHAIN_FILE=OFF", "-DANDROID_STL=c++_shared")
            }
        }
        ndk { abiFilters += listOf("arm64-v8a") }
    }

    externalNativeBuild {
        cmake {
            version = "3.22.1"
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }

    signingConfigs {
        getByName("debug") {
            storeFile = file("../debug.keystore")
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
    }

    buildTypes {
        debug {
            isMinifyEnabled = false
            isDebuggable = true
            isJniDebuggable = true
            signingConfig = signingConfigs.getByName("debug")
        }
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    sourceSets {
        getByName("main") {
            java.srcDir("../deps/SDL/android-project/app/src/main/java")
            jniLibs.srcDirs("src/main/jniLibs", layout.buildDirectory.dir("generated/openxr-loader/jniLibs"))
        }
    }

    lint {
        abortOnError = false
        // Release builds run a separate "lintVital" pass that's fatal
        // regardless of abortOnError for a few Play Store policy checks --
        // ExpiredTargetSdkVersion is one of them. Moot here: this is a
        // sideloaded VR app, never distributed through Play.
        disable += "ExpiredTargetSdkVersion"
    }

    // the bundled pk3s are already zip archives: store them uncompressed so the
    // launcher can size-check and stream them out quickly
    androidResources {
        noCompress += listOf("pk3", "d0pk")
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
            keepDebugSymbols.add("**/*.so")
        }
    }
}

configurations {
    create("openxrLoaderAar")
}

dependencies {
    implementation("org.khronos.openxr:openxr_loader_for_android:1.1.60")
    add("openxrLoaderAar", "org.khronos.openxr:openxr_loader_for_android:1.1.60@aar")
}

// Pull libopenxr_loader.so out of the Khronos AAR so it is packaged as a plain jniLib
// (same trick as QuakeQuest): the engine dlopen()s it by name at runtime.
tasks.register<Copy>("extractOpenXrLoader") {
    from({ zipTree(configurations.getByName("openxrLoaderAar").singleFile) }) {
        include("prefab/modules/openxr_loader/libs/android.arm64-v8a/libopenxr_loader.so")
        eachFile { path = "arm64-v8a/libopenxr_loader.so" }
        includeEmptyDirs = false
    }
    into(layout.buildDirectory.dir("generated/openxr-loader/jniLibs"))
}
tasks.named("preBuild") { dependsOn("extractOpenXrLoader") }
