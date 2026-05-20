# MobileCAD

A native Android CAD application built with C++20 and Vulkan, using Android NDK NativeActivity. No X11, no GLFW — runs directly on Android with zero translation overhead.

## Features

- C++20 with strict C++ Core Guidelines compliance
- Vulkan 1.2+ for high-performance graphics
- Android NDK NativeActivity (direct ANativeWindow)
- Slang shader language with host compilation
- Manual APK packaging (aapt2 + zipalign + apksigner), no Gradle
- Validation layers for debugging
- Optimized for mobile GPUs

## Prerequisites

- **Android NDK**: r26+ (includes Clang, Vulkan headers, native_app_glue)
- **Android SDK**: Command-line tools, build-tools, platform-sdk-34
- **Build Tools**: Gradle 8+, CMake 3.20+, Ninja
- **Shader Compiler**: Either `slangc` (recommended) or `glslangValidator` on the host

### Setup

```bash
# Set ANDROID_NDK_HOME and ANDROID_HOME
export ANDROID_NDK_HOME=$HOME/Android/Sdk/ndk/26.1.10909125
export ANDROID_HOME=$HOME/Android/Sdk

# Verify NDK is available
ls $ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake
```

### Slang Shader Compiler

```bash
# Build Slang from source (host machine)
git clone https://github.com/shader-slang/slang.git
cd slang && git submodule update --init --recursive
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
sudo cp bin/slangc /usr/local/bin/

# Or use glslangValidator if available in your distro
```

## Building

> **ARM64 only.** All builds must use the hybrid toolchain at
> `~/AI/arm64-ndk/arm64-android.toolchain.cmake`. Do NOT use Gradle — the `android/`
> directory is tracked but its Gradle CMake paths are stale (`android/app/src/main/cpp/`
> does not exist) and `android/app/src/main/AndroidManifest.xml` has `hasCode=false`.

```bash
# From project root (inside PRoot/Termux)

# 1. Compile shaders
./scripts/compile_shaders.sh

# 2. Configure — ARM64 hybrid toolchain
cmake -S ndk-build -B build \
  -DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Debug -G Ninja

# 3. Build .so
ninja -C build   # → build/libmobilecad.so

# 4. Generate classes.dex (before packaging)
rm -rf /tmp/mcad-dex /tmp/mcad-classes /tmp/mcad-java
mkdir -p /tmp/mcad-java /tmp/mcad-classes /tmp/mcad-dex
echo 'package com.mobilecad; public class R {}' > /tmp/mcad-java/R.java
echo 'package com.mobilecad; public class BuildConfig {}' > /tmp/mcad-java/BuildConfig.java
javac -source 8 -target 8 -bootclasspath "$ANDROID_JAR" -d /tmp/mcad-classes /tmp/mcad-java/*.java 2>/dev/null
/data/data/com.termux/files/home/android-sdk/build-tools/34.0.0/d8 \
  --lib "$ANDROID_JAR" --output /tmp/mcad-dex /tmp/mcad-classes/com/mobilecad/*.class
cp /tmp/mcad-dex/classes.dex staging/

# 5. Package APK (aapt2 → zip → zipalign → apksigner)
rm -rf staging && mkdir -p staging/lib/arm64-v8a
cp build/libmobilecad.so staging/lib/arm64-v8a/
cp "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" staging/lib/arm64-v8a/
cp -r assets staging/
cp /tmp/mcad-dex/classes.dex staging/
# staging/AndroidManifest.xml is tracked in the repo and must be present
aapt2 link -I "$ANDROID_JAR" --manifest staging/AndroidManifest.xml -A staging/assets -o mobilecad-base.apk
cd staging && zip -ur ../mobilecad-base.apk lib/arm64-v8a/ && cd ..
zipalign -v 4 mobilecad-base.apk mobilecad-aligned.apk
apksigner sign --ks debug.keystore --ks-pass pass:android --key-pass pass:android \
  --ks-key-alias androiddebugkey mobilecad-aligned.apk
```

### Install on Device

```bash
adb install -r mobilecad-aligned.apk
```

### Launch

```bash
adb shell "am force-stop com.mobilecad; am start -n com.mobilecad/android.app.NativeActivity"
```

### View Logs

```bash
adb logcat -v threadtime | grep MobileCAD:
```

### Troubleshooting

If ADB can't connect, the device is on `192.168.0.131:42113`:

```bash
adb connect 192.168.0.131:42113
```

Shaders compile to `shaders/compiled/*.spv` via `scripts/compile_shaders.sh`. The compiled `.spv` files are placed in `assets/shaders/` before APK packaging — no Gradle step.

## Project Structure

```
MobileCAD/
├── CMakeLists.txt              # Root build config (shader compilation)
├── ndk-build/CMakeLists.txt    # ★ Primary CMake entry (NDK, linker flags, vk_video)
├── src/
│   ├── main.cpp                # NDK entry point — extern "C" android_main
│   ├── app/CADApp.{h,cpp}      # Rendering orchestrator
│   ├── rendering/
│   │   ├── VulkanContext.{h,cpp}
│   │   ├── Swapchain.{h,cpp}
│   │   ├── Pipeline.{h,cpp}
│   │   └── shaders/*.slang     # Slang sources → slangc → .spv
│   └── utils/math/{Vec3,Mat4}.h
├── scripts/compile_shaders.sh  # Standalone shader build
├── assets/shaders/*.spv        # Bundled into APK at package time
├── staging/AndroidManifest.xml # APK manifest template (tracked, not ephemeral)
└── README.md
```

`android/` is tracked but its Gradle CMake paths and its `AndroidManifest.xml`
(`hasCode=false`, wrong `src/main/cpp/` path) are **not used** by the ARM64 build.
Do not reference `android/app/src/main/assets/shaders/` or `app/build/outputs/apk/`.

## Development

### Prerequisites

- Android SDK: `ANDROID_JAR` at `platforms/android-34/android.jar`
- Android NDK r26 at `ANDROID_NDK_HOME` (env var)
- `slangc` on PATH (build Slang via `scripts/build_slang.sh` if needed)

### Code Style

- C++20, Allman braces, 4-space indent, left pointer alignment (`int* p`)
- `clang-format -i src/**/*.cpp src/**/*.h`

### Debugging

```bash
adb logcat -v threadtime | grep MobileCAD:
adb logcat -v threadtime | grep MobileCAD: && adb logcat AndroidRuntime:* *:E
```

Key logcat milestones:
- `android_main ENTERED` — NDK lifecycle working
- `CADApp::init: starting` → `CADApp::init: done` — Vulkan init OK
- `drawFrame step-5: acquire result=0` — swapchain image acquired
- `onWindowResized step-7: initFramebuffers` — resize rebuild OK

### Deeper docs

See [CLAUDE.md](CLAUDE.md) for architecture, Vulkan object lifetimes, error-handling patterns, and
C++ Core Guidelines compliance. See [BUILD_FIXES.md](BUILD_FIXES.md) for the full history of
build issues and their fixes.

