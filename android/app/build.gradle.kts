plugins {
    id("com.android.application")
}

android {
    namespace = "com.mobilecad.app"
    compileSdk = 34
    ndkVersion = "26.1.10909125"

    defaultConfig {
        applicationId = "com.mobilecad.app"
        minSdk = 34
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0"

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++20")
                arguments += listOf(
                    "-DANDROID_STL=c++_static",
                    "-DANDROID_TOOLCHAIN=clang",
                    "-DCMAKE_MAKE_PROGRAM=/usr/bin/ninja"
                )
            }
        }

        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            ndk {
                debugSymbolLevel = "FULL"
            }
        }
        debug {
            isJniDebuggable = true
        }
    }

    externalNativeBuild {
        cmake {
            path = file("CMakeLists.txt")
            // Skip version check to use system cmake (3.31.6) instead of bundled 3.22.1
        }
    }

    // ndkPath set via local.properties

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    lint {
        abortOnError = false
    }

    packaging {
        jniLibs {
            useLegacyPackaging = false
        }
    }
}

dependencies {
    // No Java code needed for NativeActivity
}
