# MobileCAD — Session Context

## Project Overview

- **What:** Native Android CAD app using C++20 and Vulkan via Android NDK NativeActivity
- **Target:** ARM64 (aarch64-linux-android34, Android 14+)
- **Build environment:** PRoot Ubuntu on Termux (Samsung Galaxy S23 Ultra)
- **No Gradle** — custom ARM64 hybrid toolchain + manual APK packaging

---

## Session Summary

### Build Issues Fixed (in order)

1. **Missing Vulkan video headers** — NDK r26's `vulkan_core.h` includes `vk_video/vulkan_video_*.h` but the sysroot doesn't contain them. NDK's own copy under `sources/third_party/` is incomplete (missing AV1/VP9). **Fix:** Copy the complete set from `external/slang/build/_deps/vulkan_headers-src/include/vk_video/` into the build tree at configure time (`ndk-build/CMakeLists.txt`).

2. **Duplicate CMake shader targets** — Root `CMakeLists.txt` and `app/src/main/cpp/CMakeLists.txt` both defined `compile_shaders`. **Fix:** Use `ndk-build/CMakeLists.txt` as sole entry point (`cmake -S ndk-build -B build`).

3. **Deprecated NDK event loop** — `src/main.cpp` used removed `pendingCommand`/`android_app_clear_pending_cmd`. **Fix:** Replaced with `ALooper_pollOnce()` + `source->process(state, source)`.

4. **Linker `-lgcc` not found** — Host Clang 20 expects GNU libgcc absent from Android sysroot. **Fix:** Added `-rtlib=compiler-rt`, `-unwindlib=libunwind`, `-resource-dir`, and `-L` flags in `ndk-build/CMakeLists.txt`; linked `libclang_rt.builtins-aarch64-android.a` + `libunwind.a` from NDK's toolchain.

5. **`android_main` C++ name mangling** — Without `extern "C"`, the system's weak default stub is called instead of our function. **Fix:** Added `extern "C" __attribute__((visibility("default")))`.

6. **Missing `libc++_shared.so`** — Android NDK's shared C++ runtime wasn't bundled in the APK, causing `dlopen` to fail silently and the app to close immediately. **Fix:** Copy `libc++_shared.so` from NDK sysroot into `lib/arm64-v8a/` in APK.

7. **APK packaging** — `package_apk.sh` had `aapt2` compatibility issues (old `-M` flag, missing resources). **Fix:** Manual packaging: `aapt2 link` (manifest + assets) → `zip` native `.so` files → `zipalign` → `apksigner`. Manifest uses hardcoded strings, no resource references.

### Launch Crash Bugs — Runtime Fixes

After confirming the build succeeded and an APK could launch, 7 additional crash-class bugs were identified and fixed (see commit `d0b2da9`):

| # | File | Bug | Fix |
|---|------|-----|-----|
| **1** | `src/main.cpp` | `g_assetManager` never set — `Pipeline::loadShader()` fell back to filesystem path `shaders/simple.vert.spv`, which doesn't exist on Android (working directory is `/`). This threw `std::runtime_error` inside `CADApp::init` before any frame was drawn. | Call `rendering::Pipeline::setAssetManager(app->activity->assetManager)` in `APP_CMD_INIT_WINDOW` **before** constructing `CADApp`. |
| **2** | `src/main.cpp` | CADApp was never constructed — `main.cpp` was a logging/event-stub stub. `new CADApp(window)` now happens in `APP_CMD_INIT_WINDOW`; `drawFrame()` called each loop iteration when initialized; `APP_CMD_TERM_WINDOW` and exit-path cleanup wired. | Rewrote `main.cpp` to construct CADApp, call `drawFrame()`, and manage lifecycle. |
| **3** | `src/rendering/Swapchain.cpp:80` | `compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR` — most Android surface types do **not** support the OPAQUE flag. `vkCreateSwapchainKHR` returned a failure result. | Changed to `VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR`. |
| **4** | `src/rendering/VulkanContext.cpp:273` | `createLogicalDevice()` created the `VkDevice` but never called `vkGetDeviceQueue()`. Both `m_graphicsQueue` and `m_presentQueue` remained `VK_NULL_HANDLE`. `vkQueueSubmit(handle=VK_NULL_HANDLE)` caused a crash. | Added `vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue)` and set `m_presentQueue = m_graphicsQueue` after `vkCreateDevice`. |
| **5** | `src/app/CADApp.cpp:398` | `VkClearValue clearColor = {0.1f, 0.1f, 0.1f, 1.0f}` — `VkClearValue` is a union; this aggregate init zero-initialises the struct then writes only `color.float32[0]`, leaving the rest as zero (technically UB in C++). | Changed to `VkClearValue clearColor{}; clearColor.color = {{0.1f, 0.1f, 0.1f, 1.0f}};`. |
| **6** | `src/rendering/VulkanContext.cpp:104` | `m_surface` was never destroyed in `~VulkanContext()`. The surface was leaked every time. | Added `vkDestroySurfaceKHR(m_instance, m_surface, nullptr)` **before** `vkDestroyInstance`. |
| **7** | `src/main.cpp` | `<android/log.h>` was inadvertently omitted from the rewritten `main.cpp`, causing compile errors for `ANDROID_LOG_INFO` / `ANDROID_LOG_ERROR` macros. | Added `#include <android/log.h>`. |

### Files Modified (session)

| File | Key Changes |
|------|------------|
| `ndk-build/CMakeLists.txt` | vk_video header copy, Vulkan find_package, android_native_app_glue, link opts for compiler-rt/libunwind, resource-dir, -L paths |
| `src/main.cpp` | CADApp construction/destruction, `Pipeline::setAssetManager`, `drawFrame()` in render loop, `APP_CMD_WINDOW_RESIZED` + `APP_CMD_TERM_WINDOW` handling |
| `src/rendering/VulkanContext.cpp` | `vkGetDeviceQueue()` after `vkCreateDevice()`, `vkDestroySurfaceKHR()` in destructor |
| `src/rendering/Swapchain.cpp` | `VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR` |
| `src/app/CADApp.cpp` | `VkClearValue` explicit union member assignment |

---

## Current State

### Build succeeds
```bash
cmake -S ndk-build -B build \
  -DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake \
  -G Ninja
ninja -C build        # → build/libmobilecad.so (~443 KB)
```

### APK built, signed, delivered
- **APK:** `mobilecad-aligned.apk` (~611 KB, signed with debug keystore)
- **Location:** `~/storage/shared/Download/mobilecad-aligned.apk`
- **Contains:**
  - `AndroidManifest.xml` (binary, compiled by aapt2)
  - `assets/shaders/simple.vert.spv`, `simple.frag.spv`
  - `lib/arm64-v8a/libmobilecad.so`
  - `lib/arm64-v8a/libc++_shared.so`
- **Manifest:** `package=com.mobilecad`, `android.app.lib_name=mobilecad`, Vulkan required, `minSdkVersion=34`, debuggable
- **Status:** All 7 previously-catastrophic bugs fixed. App should now reach `drawFrame()` and render the colored triangle. **Pending on-device verification.**

---

## Build Commands (reference)

```bash
# Inside PRoot /root/AI/MobileCAD

# 1. Shaders
./scripts/compile_shaders.sh

# 2. Configure
cmake -S ndk-build -B build \
  -DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake \
  -G Ninja

# 3. Build
ninja -C build

# 4. Package APK
SDK=/data/data/com.termux/files/home/android-sdk
rm -rf staging && mkdir -p staging/lib/arm64-v8a staging/assets/shaders

cp build/libmobilecad.so staging/lib/arm64-v8a/
cp "$SDK/ndk/26.1.10909125/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" staging/lib/arm64-v8a/
cp shaders/compiled/simple.frag.spv staging/assets/shaders/
cp shaders/compiled/simple.vert.spv staging/assets/shaders/

cat > staging/AndroidManifest.xml <<'EOF'
[use the manifest from staging/AndroidManifest.xml in the repo]
EOF

mkdir -p /tmp/aapt2_work
aapt2 link -o /tmp/aapt2_work/base.apk \
  --manifest staging/AndroidManifest.xml \
  -A staging/assets \
  -I "$SDK/platforms/android-34/android.jar"

cd staging && zip -ur ../build/mobilecad-base.apk lib/arm64-v8a/ && cd ..
zipalign -v 4 mobilecad-base.apk mobilecad-aligned.apk
apksigner sign --ks debug.keystore --ks-pass pass:android --key-pass pass:android \
  --ks-key-alias androiddebugkey mobilecad-aligned.apk
apksigner verify mobilecad-aligned.apk

# Copy to shared storage
cp mobilecad-aligned.apk ~/storage/shared/Download/`
```

---

## Key Paths

| Path | Purpose |
|------|---------|
| `ndk-build/CMakeLists.txt` | Primary CMake entry (vk_video copy, find_package, link opts) |
| `src/main.cpp` | NDK entry point — CADApp lifecycle, asset manager, render loop |
| `src/app/CADApp.cpp` / `.h` | CADApp (Vulkan init, drawFrame, cleanup, resize) |
| `src/rendering/VulkanContext.cpp` / `.h` | VulkanContext (instance, device, surface, queues, command pool, sync) |
| `src/rendering/Swapchain.cpp` / `.h` | Swapchain creation and management |
| `src/rendering/Pipeline.cpp` / `.h` | Shader loading (AAssetManager fallback to filesystem), shader module creation |
| `assets/shaders/` | SPIR-V shaders (`simple.vert.spv`, `simple.frag.spv`) |
| `BUILD_FIXES.md` | Detailed documentation of build fixes |
| `MOBILECAD_CONTEXT.md` | This file — full session history |

---

## Known Unknowns / Open Questions

- None remaining. All identified crash-class bugs are fixed. The app is expected to render a coloured triangle on launch. **Verify on device.**

## What the Next Session Should Do

1. `adb install ~/storage/shared/Download/mobilecad-aligned.apk` (from Termux, outside PRoot)
2. `adb logcat MobileCAD:* *:S` and confirm `android_main ENTERED` → `CADApp created: initialized=1` → frames rendering
3. If anything fails, inspect `adb logcat -b crash` and `/data/tombstones/`
4. After triangle starts working, begin CAD geometry MVP: vertex buffers uploaded with `vkCreateBuffer` / `vkMapMemory`, transform uniforms via push constants, and a basic camera matrix.
