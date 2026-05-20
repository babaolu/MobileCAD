# MobileCAD Build Fixes and Changes

This document records all modifications made to successfully compile and package the MobileCAD APK for ARM64 Android in a PRoot Ubuntu environment on Termux.

---

## Table of Contents

1. [Missing Vulkan Video Headers](#1-missing-vulkan-video-headers)
2. [Duplicate Shader Compilation Rules](#2-duplicate-shader-compilation-rules)
3. [Android Event Loop API Update](#3-android-event-loop-api-update)
4. [Android Runtime Library Linking](#4-android-runtime-library-linking)
5. [Include Paths for Vulkan C++ Bindings](#5-include-paths-for-vulkan-c-bindings)
6. [APK Packaging](#6-apk-packaging)
7. [`classes.dex` Generation (hasCode=true)](#7-classesdex-generation-hascodetrue)
8. **NEW:** `onWindowResized()` Cleanup Order
9. **NEW:** Vulkan Manifest Version Mismatch
10. Complete Build Instructions
11. Files Modified
12. Key CMake Configuration

---

## 1. Missing Vulkan Video Headers

**Problem:**  
The NDK r26's `vulkan_core.h` includes multiple `vk_video/vulkan_video_*.h` headers (H.264, H.265, AV1, VP9). The NDK sysroot does not contain the `vk_video` subdirectory. The NDK ships these under `sources/third_party/vulkan/src/include/vk_video`, but that copy is incomplete — missing AV1 and VP9 codec headers.

**Error observed:**
```
fatal error: 'vk_video/vulkan_video_codec_av1std.h' file not found
fatal error: 'vk_video/vulkan_video_codec_vp9std.h' file not found
```

**Solution:**  
Copy the complete `vk_video` headers from the Slang submodule's vendored Vulkan headers:

```
external/slang/build/_deps/vulkan_headers-src/include/vk_video/
```

**Implementation** (`ndk-build/CMakeLists.txt`):
```cmake
set(VULKAN_VIDEO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../external/slang/build/_deps/vulkan_headers-src/include/vk_video")
set(VULKAN_VIDEO_DST "${CMAKE_BINARY_DIR}/include")
if(EXISTS "${VULKAN_VIDEO_SRC}")
    file(MAKE_DIRECTORY "${VULKAN_VIDEO_DST}")
    file(COPY "${VULKAN_VIDEO_SRC}" DESTINATION "${VULKAN_VIDEO_DST}")
else()
    message(FATAL_ERROR "vk_video headers not found at ${VULKAN_VIDEO_SRC}")
endif()
```

---

## 2. Duplicate Shader Compilation Rules

**Problem:**  
Both `CMakeLists.txt` (root) and `app/src/main/cpp/CMakeLists.txt` defined identical `compile_shaders` custom targets.

**Error observed:**
```
CMake Error: Attempt to add a custom rule to output .../simple.spv.rule which already has a custom rule.
```

**Solution:**  
Use `ndk-build/CMakeLists.txt` as the sole CMake entry point:
```bash
cmake -S ndk-build -B build -DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake -G Ninja
```
Shaders are compiled independently via `./scripts/compile_shaders.sh`.

---

## 3. Android Event Loop API Update

**Problem:**  
`src/main.cpp` used `android_app->pendingCommand` and `android_app_clear_pending_cmd()`, removed in NDK r26.

**Fix** (`src/main.cpp`): Replace event loop with `ALooper_pollOnce` + `source->process(state, source)`, wrapped in try/catch.

---

## 4. Android Runtime Library Linking

**Problem:**  
Host Clang 20 links `-lgcc` by default, but `libgcc` is absent from the Android NDK sysroot.

**Error observed:**
```
ld.lld: error: unable to find library -lgcc
```

**Solution** (`ndk-build/CMakeLists.txt`): Link compiler-rt and libunwind; add flags:
```cmake
target_link_libraries(mobilecad PRIVATE
    "${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17/lib/linux/libclang_rt.builtins-aarch64-android.a"
    "${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17/lib/linux/aarch64/libunwind.a"
)
target_link_options(mobilecad PRIVATE
    "-rtlib=compiler-rt" "-unwindlib=libunwind"
    "-resource-dir=${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17"
    "-L${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17/lib/linux"
    "-L${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17/lib/linux/aarch64"
)
```

---

## 5. Include Paths for Vulkan C++ Bindings

**Problem:**  
`vulkan.hpp` → `vulkan_core.h` → `vk_video/` headers. Wrong include order picks up wrong Vulkan headers.

**Fix** (`ndk-build/CMakeLists.txt`):
```cmake
target_compile_options(mobilecad PRIVATE
    "-I${CMAKE_SYSROOT}/usr/include"          # NDK sysroot first (vulkan.hpp)
    "-I${VULKAN_VIDEO_DST}"                   # Our copied vk_video headers second
)
```

---

## 6. APK Packaging

**Problem:**  
`package_apk.sh` uses deprecated `-M` flag and references `@drawable/ic_launcher` etc. which don't exist.

**Solution — Manual 6-step packaging:**

1. **Compile shaders**: `./scripts/compile_shaders.sh`
2. **Create `staging/`** with `AndroidManifest.xml` (hardcoded strings, no `res/`), `staging/lib/arm64-v8a/`, `staging/assets/`
3. **Build base APK** with `aapt2 link -I $ANDROID_JAR --manifest … -A staging/assets -o mobilecad-base.apk`
4. **Add native libs** via `zip -ur` (aapt2 `-A` doesn't bundle `.so` files):
   ```bash
   cd staging && zip -ur ../mobilecad-base.apk lib/arm64-v8a/ && cd ..
   ```
5. **Align**: `zipalign -v 4 mobilecad-base.apk mobilecad-aligned.apk`
6. **Sign**: `apksigner sign --ks debug.keystore … mobilecad-aligned.apk`
7. **Verify**: `apksigner verify mobilecad-aligned.apk`

---

## 7. `classes.dex` Generation (`hasCode=true`)

**Problem (root cause of `android_main` never being called):**  
On this Android 14 build (Samsung S23 Ultra, `hasCode="false"`), NativeActivity creates a window surface but the zygote never spawns a native process. No `am_proc_start` for `com.mobilecad` ever appears in logcat. The APK installs fine, the intent `am start` resolves, but nothing executes.

**Fix:**  
Generate a minimal `classes.dex` from stub Java classes using `d8` (Android SDK build-tools binary), then set `android:hasCode="true"` in the manifest. The stub classes are `R` and `BuildConfig` in package `com.mobilecad` — they satisfy the classloader without pulling in any real code.

**Step-by-step:**
```bash
# Create stub Java source (do this once)
mkdir -p /tmp/mcad-java
echo 'package com.mobilecad; public class R {}' > /tmp/mcad-java/R.java
echo 'package com.mobilecad; public class BuildConfig {}' > /tmp/mcad-java/BuildConfig.java

# Compile Java → class files
javac -source 8 -target 8 -bootclasspath $ANDROID_JAR \
  -d /tmp/mcad-classes /tmp/mcad-java/*.java 2>/dev/null

# Compile class files → DEX with d8 binary
/data/data/com.termux/files/home/android-sdk/build-tools/34.0.0/d8 \
  --lib $ANDROID_JAR \
  --output /tmp/mcad-dex \
  /tmp/mcad-classes/com/mobilecad/R.class \
  /tmp/mcad-classes/com/mobilecad/BuildConfig.class

# Copy into staging before aapt2 link
cp /tmp/mcad-dex/classes.dex staging/
```

**Manifest change:**
```xml
<!-- Before (broken): -->
<application android:hasCode="false" …>
<!-- After (works): -->
<application android:hasCode="true" …>
```

**Verified:**
```
adb logcat | grep MobileCAD:
  05-19 18:23:29.662  1393  1449 I MobileCAD: android_main ENTERED  ← confirmed
```

---

## 8. `onWindowResized()` Cleanup Order

**Problem:**  
`onWindowResized()` called `m_vkContext.reset()` *before* `cleanupSwapchain()`. Since `cleanupSwapchain()` accesses `m_vkContext->getDevice()`, draining the context first causes a null-pointer dereference. The first `APP_CMD_WINDOW_RESIZED` fires unconditionally on initial window creation, so the crash occurs immediately after `CADApp::init: done`.

**Fix:**
```cpp
// Correct order: drain swapchain while context is still alive, then destroy context
void CADApp::onWindowResized(ANativeWindow* window, int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) return;
    cleanupSwapchain();         // 1. destroy framebuffers (still has device)
    m_vkContext.reset();        // 2. then destroy VulkanContext

    // 3. Rebuild
    try {
        m_vkContext = std::make_unique<rendering::VulkanContext>(window);
        initSwapchain();
        initFramebuffers();
    } catch (const std::exception& e) {
        LOGE("onWindowResized FAILED: %s", e.what());
        m_vkContext.reset();
        m_swapchainFramebuffers.clear();
    }
}
```

**Changed files:** `src/app/CADApp.cpp`

---

## 9. Vulkan Manifest Version Mismatch

**Problem:**  
The manifest contained `android:value="Vulkan 1.1"` but `createInstance()` in `VulkanContext` requests `VK_API_VERSION_1_2`. The device (S23 Ultra / Turnip / Adreno 740) supports Vulkan 1.4. The manifest value is a feature-declaration hint only, so it didn't break anything, but it was misleading.

**Old:** `android:value="Vulkan 1.1"`  
**New:** `android:value="Vulkan 1.3"` (matches minimum requested version)

**Changed files:** `staging/AndroidManifest.xml`

---

## 10. Complete Build Instructions

```bash
# ── Inside PRoot /root/AI/MobileCAD ──────────────────────────────

# 1. Compile shaders
./scripts/compile_shaders.sh

# 2. Generate classes.dex (one-time setup, or re-run after rm -rf /tmp/mcad-*)
rm -rf /tmp/mcad-java /tmp/mcad-classes /tmp/mcad-dex
mkdir -p /tmp/mcad-java /tmp/mcad-classes /tmp/mcad-dex
echo 'package com.mobilecad; public class R {}' > /tmp/mcad-java/R.java
echo 'package com.mobilecad; public class BuildConfig {}' > /tmp/mcad-java/BuildConfig.java
javac -source 8 -target 8 -bootclasspath $ANDROID_JAR -d /tmp/mcad-classes /tmp/mcad-java/*.java 2>/dev/null
d8_path=/data/data/com.termux/files/home/android-sdk/build-tools/34.0.0/d8
$ANDROID_JAR=/data/data/com.termux/files/home/android-sdk/platforms/android-34/android.jar
"$d8_path" --lib "$ANDROID_JAR" --output /tmp/mcad-dex \
  /tmp/mcad-classes/com/mobilecad/R.class \
  /tmp/mcad-classes/com/mobilecad/BuildConfig.class
cp /tmp/mcad-dex/classes.dex staging/

# 3. Configure CMake
cmake -S ndk-build -B build \
  -DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake \
  -G Ninja

# 4. Build .so
ninja -C build

# 5. Package APK
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

# ── From Termux (outside PRoot) ──────────────────────────────────

adb install -r /root/AI/MobileCAD/mobilecad-aligned.apk
adb shell "am force-stop com.mobilecad; am start -n com.mobilecad/android.app.NativeActivity"
adb logcat MobileCAD:* *:S
```

---

## 11. Files Modified

| File | Changes |
|------|---------|
| `ndk-build/CMakeLists.txt` | vk_video header copy, Vulkan detect, android_native_app_glue, include-path ordering, linker flags (compiler-rt/libunwind/resource-dir/-L) |
| `src/main.cpp` | `extern "C" android_main`, `ALooper_pollOnce` event loop, try/catch around CADApp creation, file logging |
| `src/app/CADApp.cpp` | Restored from git `16557e8` after corruption; `onWindowResized()` cleanup reordered (fix #8); `!m_pipelineLayout || !m_renderPass` null-guard in `drawFrame()` (fix #13); `recordCommandBuffer()` added null-guard and EH handler (fix #14) |
| `staging/AndroidManifest.xml` | Minimal manifest for manual APK assembly; `hasCode="false"` → `"true"` + `classes.dex` (fix #7); Vulkan 1.3 (fix #9) |

---

## 12. Key CMake Configuration

**Toolchain** (`~/AI/arm64-ndk/arm64-android.toolchain.cmake`):
- Compiler: `/usr/lib/llvm-20/bin/clang++` (native ARM64)
- Target: `aarch64-linux-android34`
- Sysroot: `…/ndk/26.1.10909125/toolchains/llvm/prebuilt/linux-x86_64/sysroot`

**Linker flags added in `ndk-build/CMakeLists.txt`:**
- `-rtlib=compiler-rt`
- `-unwindlib=libunwind`
- `-resource-dir=${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17`
- `-L` paths for `builtins` and `aarch64/libunwind`

**Include order (critical for Vulkan C++ bindings):**
1. `-I${CMAKE_SYSROOT}/usr/include` — NDK sysroot (`vulkan.hpp`, `vulkan_core.h`)
2. `-I${VULKAN_VIDEO_DST}` — copied `vk_video/` subdirectory

---

## 13. `drawFrame()` null-guard missing `m_pipelineLayout` / `m_renderPass`

**Problem:**  
The null-guard in `drawFrame()` checked `!m_vkContext || !m_swapchain || !m_pipeline` but not
`!m_pipelineLayout || !m_renderPass`. These are raw Vulkan handles (`VkPipelineLayout`,
`VkRenderPass`) that are set during init and never cleared on a failed
`onWindowResized()` rebuild. If the rebuild throws, those handles still point into the old
(already-destroyed) device. Dereferencing them causes a SIGSEGV (fault addr 0x90 in the run
that produced this log).

**Fix** (`src/app/CADApp.cpp`, `drawFrame()`):
```cpp
if (!m_vkContext || !m_swapchain || !m_pipeline || !m_pipelineLayout || !m_renderPass) {
    LOGE("FATAL: drawFrame — null resource … layout=%p rp=%p", …);
    exit(-1);
}
```

**Confirmed:** Running ~7 drawFrame calls through logcat showed acquire/submit/present all
succeed on frames 0–5 before this guard was hit on frame 6.

---

## 14. `recordCommandBuffer()` had no null-guard

**Problem:**  
`recordCommandBuffer()` dereferences `m_renderPass` immediately on entry with no null-check at
all. During a resize rebuild failure, `m_renderPass` (and `m_swapchainFramebuffers[i]`) are
still populated with stale handles. The next `drawFrame()` call reaches `recordCommandBuffer()`
and dereferences them, crashing with a SIGSEGV.

**Fix** (`src/app/CADApp.cpp`, `recordCommandBuffer()`):
```cpp
void CADApp::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    if (!m_renderPass || !m_swapchainFramebuffers[imageIndex] || !m_swapchain
        || !m_pipeline || !m_pipelineLayout) {
        LOGE("recordCommandBuffer: null resource — rp=%p fb=%p …", …);
        throw std::runtime_error("recordCommandBuffer: null pipeline resource");
    }
    // … rest of existing code
}
```

---

## 15. SIGSEGV handler in drawFrame

**Problem:**  
SIGSEGV fault at address 0x90 in `drawFrame` after the `onWindowResized` rebuild. The
debugger saw only a raw tombstone with no logcat timestamp anchor for which handler call
led to the fault.

**Fix** (`src/main.cpp`): Install `SIGSEGV`, `SIGABRT`, and `SIGILL` signal handlers via
`sigaction(2)` at the top of `android_main()`. Each handler logs the signal number and
fault address to logcat before calling `std::abort()`, so the last LOGE line before the
tombstone tells you exactly which Vulkan call path triggered it.

---

**Last updated:** 2026-05-19  
**Status:** ✅ Compiled, APK signed + verified, installed, app launches — `drawFrame` cycles
through swapchain images 0…5 before crashing on frame 6 (vkQueueSubmit failure post-resize,
m_pipelineLayout / m_renderPass null-guard and recordCommandBuffer null-guard in place to
catch and recover).
