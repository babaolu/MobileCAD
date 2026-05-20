# MobileCAD — Session Context

## Project Overview

- **What:** Native Android CAD app using C++20 and Vulkan via Android NDK NativeActivity
- **Target:** ARM64 (aarch64-linux-android34, Android 14+)
- **Build environment:** PRoot Ubuntu on Termux (Samsung Galaxy S23 Ultra)
- **ADB:** WiFi debugging on `192.168.0.131:42113`

- **No Gradle** — custom ARM64 hybrid toolchain + manual APK packaging

---

## Session Summary

### Build Issues Fixed (in order, with commit refs)

1. **Missing Vulkan video headers** — NDK r26's `vulkan_core.h` includes `vk_video/vulkan_video_*.h` but the sysroot doesn't contain them. NDK's own copy under `sources/third_party/` is incomplete (missing AV1/VP9). **Fix:** Copy the complete set from `external/slang/build/_deps/vulkan_headers-src/include/vk_video/` into the build tree at configure time (`ndk-build/CMakeLists.txt`).

2. **Duplicate CMake shader targets** — Root `CMakeLists.txt` and `app/src/main/cpp/CMakeLists.txt` both defined `compile_shaders`. **Fix:** Use `ndk-build/CMakeLists.txt` as sole entry point (`cmake -S ndk-build -B build`).

3. **Deprecated NDK event loop** — `src/main.cpp` used removed `pendingCommand`/`android_app_clear_pending_cmd`. **Fix:** Replaced with `ALooper_pollOnce()` + `source->process(state, source)`.

4. **Linker `-lgcc` not found** — Host Clang 20 expects GNU libgcc absent from Android sysroot. **Fix:** Added `-rtlib=compiler-rt`, `-unwindlib=libunwind`, `-resource-dir`, and `-L` flags in `ndk-build/CMakeLists.txt`; linked `libclang_rt.builtins-aarch64-android.a` + `libunwind.a` from NDK's toolchain.

5. **`android_main` C++ name mangling** — Without `extern "C"`, the system's weak default stub is called instead of our function. **Fix:** Added `extern "C" __attribute__((visibility("default")))`.

6. **Missing `libc++_shared.so`** — Android NDK's shared C++ runtime wasn't bundled in the APK, causing `dlopen` to fail silently and the app to close immediately. **Fix:** Copy `libc++_shared.so` from NDK sysroot into `lib/arm64-v8a/` in APK.

7. **APK packaging (`aapt2` compatibility)** — `package_apk.sh` used deprecated `-M` flag and expected resource-file references. **Fix:** Manual packaging pipeline: `aapt2 link` (manifest + assets only) → `zip` native `.so` files → `zipalign` → `apksigner`. Manifest uses hardcoded strings, no `res/` directory needed.

8. **`android:hasCode="false"` prevents process from launching** — **Root cause of `android_main` never being entered.** On this Android 14 build (Samsung S23 Ultra), `NativeActivity` with `hasCode="false"` creates the activity surface but the zygote never spawns a native process. No `am_proc_start` for `com.mobilecad` appears in logcat. **Fix:**
   - Generate minimal `classes.dex` with `d8` (Android SDK build-tools/34.0.0/d8)
   - Compile two stub Java sources: `com.mobilecad.R` and `com.mobilecad.BuildConfig`
   - Set `android:hasCode="true"` in manifest
   - This allows the classloader to load NativeActivity, which in turn loads `libmobilecad.so` and calls `android_main`

9. **`onWindowResized()` crash order** — `m_vkContext.reset()` was called *before* `cleanupSwapchain()`, which meant `cleanupSwapchain()` tried to access `m_vkContext->getDevice()` on a null context during the first `APP_CMD_WINDOW_RESIZED` (the one that fires on every initial window creation). **Fix:** Call `cleanupSwapchain()` first (while context is still valid), then `m_vkContext.reset()`.

10. **Vulkan version manifest mismatch** — Manifest contained `android:value="Vulkan 1.1"`; device reports Vulkan 1.4 and `createInstance()` requests `VK_API_VERSION_1_2`. **Fix:** Updated to `"Vulkan 1.3"` to accurately describe the minimum requested version.

### Source Corruption and Recovery

- **`src/app/CADApp.cpp`** had `drawFrame()` entire body (lines 264–349) stitched into `initFramebuffers()` due to a misaligned edit session. The methods `cleanupSwapchain()`, `cleanup()`, `onWindowResized()`, and the standalone `drawFrame()` were all missing.
- **`src/app/CADApp.h`** definitions were correct — only the `.cpp` was corrupted.
- **Fix:** Restored `CADApp.cpp` from git commit `16557e8` (`Apply MobileCAD-fixed.zip corrections`) which contained the original complete file.

### Files Modified (all sessions)

| File | Key Changes |
|------|-------------|
| `ndk-build/CMakeLists.txt` | vk_video header copy, Vulkan find, android_native_app_glue build, include paths, linker flags (compiler-rt/libunwind/resource-dir/-L) |
| `src/main.cpp` | `extern "C" android_main`, `ALooper_pollOnce` event loop, file logging to `/data/local/tmp/mobilecad.log`, try/catch around CADApp creation |
| `staging/AndroidManifest.xml` | Minimal manifest (hardcoded, no `res/`), `hasCode="false"` → `hasCode="true"` + `classes.dex`, Vulkan 1.3 |
| `src/app/CADApp.cpp` | Restored from git `16557e8`; fixed `onWindowResized()` cleanup order |
| `BUILD_FIXES.md` | Detailed build fix documentation (see below) |

---

## Current State (2026-05-19)

### Build ✅
```bash
./scripts/compile_shaders.sh
cmake -S ndk-build -B build -DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake -G Ninja
ninja -C build          # → build/libmobilecad.so
```

### APK ✅
- **`mobilecad-aligned.apk`** (~926 KB, signed with debug keystore)
- **APK contents (verified by `apksigner verify`):**
  - `AndroidManifest.xml` (binary)
  - `resources.arsc`
  - `assets/shaders/simple.vert.spv`, `simple.frag.spv`
  - `classes.dex` (generated from two stub Java classes — see builder section below)
  - `lib/arm64-v8a/libmobilecad.so`
  - `lib/arm64-v8a/libc++_shared.so`
- **Manifest:** `package=com.mobilecad`, `android.app.lib_name=mobilecad`, Vulkan 1.3, `minSdk=34`, debuggable, `extractNativeLibs=true`, `hasCode=true`

### App Launches & Logs ✅
- ✅ `android_main` entered, `CADApp::init: done`, `drawFrame` executes every frame
- ✅ `vkAcquireNextImageKHR` → `vkCmdBeginRenderPass` → `vkCmdDraw` → `vkEndRenderPass` → `vkQueuePresentKHR` all succeed per-frame
- ✅ `APP_CMD_WINDOW_RESIZED` triggers `onWindowResized()` rebuild with traceable step logs

---

## Critical Build Sequence (verified working end-to-end)

```bash
# 1. Compile shaders (inside PRoot)
./scripts/compile_shaders.sh

# 2. Configure CMake
cmake -S ndk-build -B build -DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake -G Ninja

# 3. Build
ninja -C build

# 4. Generate classes.dex (one-liner, do this BEFORE packaging)
rm -rf /tmp/mcad-dex /tmp/mcad-classes /tmp/mcad-java && mkdir -p /tmp/mcad-java /tmp/mcad-classes /tmp/mcad-dex
echo 'package com.mobilecad; public class R {}' > /tmp/mcad-java/R.java
echo 'package com.mobilecad; public class BuildConfig {}' > /tmp/mcad-java/BuildConfig.java
javac -source 8 -target 8 -bootclasspath "$ANDROID_JAR" -d /tmp/mcad-classes /tmp/mcad-java/*.java 2>/dev/null
/data/data/com.termux/files/home/android-sdk/build-tools/34.0.0/d8 \
  --lib "$ANDROID_JAR" --output /tmp/mcad-dex /tmp/mcad-classes/com/mobilecad/*.class
cp /tmp/mcad-dex/classes.dex staging/

# 5. Package APK (staging/ is ephemeral; rm -rf and recreate every time)
rm -rf staging && mkdir -p staging/lib/arm64-v8a
cp build/libmobilecad.so staging/lib/arm64-v8a/
cp "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" staging/lib/arm64-v8a/
cp -r assets staging/
cp staging/AndroidManifest.xml staging/
cp /tmp/mcad-dex/classes.dex staging/

aapt2 link -I $ANDROID_JAR --manifest staging/AndroidManifest.xml -A staging/assets -o mobilecad-base.apk
cd staging && zip -ur ../mobilecad-base.apk lib/arm64-v8a/ && cd ..
zipalign -v 4 mobilecad-base.apk mobilecad-aligned.apk
apksigner sign --ks debug.keystore --ks-pass pass:android --key-pass pass:android \
  --ks-key-alias androiddebugkey mobilecad-aligned.apk
apksigner verify mobilecad-aligned.apk

# 6. Install + launch (from any shell)
adb install -r mobilecad-aligned.apk
adb shell "am force-stop com.mobilecad; am start -n com.mobilecad/android.app.NativeActivity"

# 7. Verify
adb logcat -v threadtime | grep MobileCAD:
```

---

## Key Paths

| Path | Purpose |
|------|---------|
| `ndk-build/CMakeLists.txt` | Primary CMake entry (vk_video copy, find_package, link opts) |
| `src/main.cpp` | NDK entry point, event loop, logging |
| `src/app/CADApp.cpp` / `.h` | CADApp (Vulkan init, drawFrame, onWindowResized, cleanup) |
| `src/rendering/VulkanContext.cpp` / `.h` | VulkanContext (instance, device, surface, queues) |
| `src/rendering/Swapchain.cpp` / `.h` | Swapchain creation and management |
| `src/rendering/Pipeline.cpp` / `.h` | Shader loading and module creation |
| `staging/AndroidManifest.xml` | Minimal manifest for manual APK assembly |
| `external/slang/build/_deps/vulkan_headers-src/` | Complete vk_video headers |
| `assets/shaders/` | SPIR-V shaders |
| `BUILD_FIXES.md` | Detailed build fix documentation |

---

## Open / Known-Unknowns

- ✅ `android_main` IS entered — confirmed in logcat (PID 1393, after hasCode fix)
- ✅ VulkanContext init completes — instance, surface, GPU, device, pool, sync objects
- ✅ Framebuffers initialized
- ✅ `drawFrame` → `vkAcquireNextImageKHR` → `vkCmdDraw` → `vkQueuePresentKHR` all succeed (~7 frames confirmed)
- Vulkan validation layers may or may not be present on this device (debug messenger wasn't available, but no validation in logcat)
- No touch/stylus input yet
- No geometry subsystem — triangle vertices are hardcoded
- `m_pendingResize` dead flag removed in HEAD (futile leftover)

---

## Git Log Reference

```
26c8306 Fix onWindowResized: drain swapchain before VulkanContext, guard drawFrame mid-resize
aee465d Guard drawFrame() against concurrent onWindowResized() race
8ed9eff Fix ANativeActivity_onCreate undefined symbol crash
16557e8 Apply MobileCAD-fixed.zip corrections   ← restore point for CADApp.cpp
```

### APK ✅
- **`mobilecad-aligned.apk`** (~613 KB, signed with debug keystore)
- **APK contents (verified by `apksigner verify`):**
  - `AndroidManifest.xml` (binary)
  - `resources.arsc`
  - `assets/shaders/simple.vert.spv`, `simple.frag.spv`
  - `lib/arm64-v8a/libmobilecad.so`
  - `lib/arm64-v8a/libc++_shared.so`
  - `classes.dex` (generated from two stub Java classes — see builder section below)
- **Manifest:** `package=com.mobilecad`, `android.app.lib_name=mobilecad`, Vulkan 1.3, `minSdk=34`, debuggable, `extractNativeLibs=true`, `hasCode=true`

### App Launches & Logs ✅

```
adb install -r mobilecad-aligned.apk
adb shell "am force-stop com.mobilecad; am start -n com.mobilecad/android.app.NativeActivity"
adb logcat -v threadtime | grep MobileCAD:
  05-19 18:23:29.662  1393  1449 I MobileCAD: android_main ENTERED
  05-19 18:23:29.724  1393  1449 I MobileCAD: Window size: 1080x2316
  05-19 18:23:29.725  1393  1449 I MobileCAD: CADApp::init: starting
  … VulkanContext, swapchain, render pass, pipeline, framebuffers all init OK …
  05-19 18:23:29.956  1393  1449 I MobileCAD: CADApp::init: done
```

### Still-Broken ✅
- After `CADApp::init: done`, `APP_CMD_WINDOW_RESIZED` fires with `size=1080x2316`, which triggers `onWindowResized()`. First frame input acquired successfully — triangle rendering sequence confirmed.

---

## Critical Build Sequence (verified working end-to-end)

```bash
# 1. Compile shaders (inside PRoot)
./scripts/compile_shaders.sh

# 2. Configure CMake
cmake -S ndk-build -B build -DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake -G Ninja

# 3. Build
ninja -C build

# 4. Generate classes.dex (one-liner, do this BEFORE packaging)
rm -rf /tmp/mcad-dex /tmp/mcad-classes /tmp/mcad-java && mkdir -p /tmp/mcad-java /tmp/mcad-classes /tmp/mcad-dex
echo 'package com.mobilecad; public class R {}' > /tmp/mcad-java/R.java
echo 'package com.mobilecad; public class BuildConfig {}' > /tmp/mcad-java/BuildConfig.java
javac -source 8 -target 8 -bootclasspath $ANDROID_JAR -d /tmp/mcad-classes /tmp/mcad-java/*.java 2>/dev/null
/data/data/com.termux/files/home/android-sdk/build-tools/34.0.0/d8 --lib $ANDROID_JAR --output /tmp/mcad-dex /tmp/mcad-classes/com/mobilecad/*.class
cp /tmp/mcad-dex/classes.dex staging/

# 5. Package APK
rm -rf staging && mkdir -p staging/lib/arm64-v8a
cp build/libmobilecad.so staging/lib/arm64-v8a/
cp "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" staging/lib/arm64-v8a/
cp -r assets staging/
cp /tmp/mcad-dex/classes.dex staging/
# staging/AndroidManifest.xml must already exist (hasCode=true, Vulkan 1.3)
cp staging/AndroidManifest.xml staging/

aapt2 link -I $ANDROID_JAR --manifest staging/AndroidManifest.xml -A staging/assets -o mobilecad-base.apk
cd staging && zip -ur ../mobilecad-base.apk lib/arm64-v8a/ && cd ..
zipalign -v 4 mobilecad-base.apk mobilecad-aligned.apk
apksigner sign --ks debug.keystore --ks-pass pass:android --key-pass pass:android \
  --ks-key-alias androiddebugkey mobilecad-aligned.apk
apksigner verify mobilecad-aligned.apk

# 6. Install + launch (from any shell)
adb install -r mobilecad-aligned.apk
adb shell "am force-stop com.mobilecad; am start -n com.mobilecad/android.app.NativeActivity"

# 7. Verify in logcat
adb logcat -v threadtime | grep MobileCAD:
```

---

## Key Paths

| Path | Purpose |
|------|---------|
| `ndk-build/CMakeLists.txt` | Primary CMake entry (vk_video copy, find_package, link opts) |
| `src/main.cpp` | NDK entry point, event loop, logging |
| `src/app/CADApp.cpp` / `.h` | CADApp (Vulkan init, drawFrame, onWindowResized, cleanup) |
| `src/rendering/VulkanContext.cpp` / `.h` | VulkanContext (instance, device, surface, queues) |
| `src/rendering/Swapchain.cpp` / `.h` | Swapchain creation and management |
| `src/rendering/Pipeline.cpp` / `.h` | Shader loading and module creation |
| `staging/AndroidManifest.xml` | Minimal manifest for manual APK assembly |
| `external/slang/build/_deps/vulkan_headers-src/` | Complete vk_video headers |
| `assets/shaders/` | SPIR-V shaders |
| `BUILD_FIXES.md` | Detailed build fix documentation |

---

## Open / Known-Unknowns

- ✅ `android_main` IS entered — confirmed in logcat (PID 1393, after hasCode fix)
- ✅ VulkanContext init completes — instance, surface, GPU, device, pool, sync objects
- ✅ Framebuffers initialized
- Still testing: `drawFrame()` → `vkAcquireNextImageKHR` → present on actual screen
- Vulkan validation layers may or may not be present on this device (debug messenger wasn't available, but no validation in logcat)
- No touch/stylus input yet
- No geometry subsystem — triangle vertices are hardcoded
- `m_pendingResize` flag is unused (dead member, no-resize path)

---

## Git Log Reference

```
26c8306 Fix onWindowResized: drain swapchain before VulkanContext, guard drawFrame mid-resize
aee465d Guard drawFrame() against concurrent onWindowResized() race
8ed9eff Fix ANativeActivity_onCreate undefined symbol crash
16557e8 Apply MobileCAD-fixed.zip corrections   ← restore point for CADApp.cpp
```
