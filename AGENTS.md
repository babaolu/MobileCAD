# AGENTS.md — MobileCAD

C++20 / Vulkan Android NDK app. ARM64-only target (`arm64-v8a`, `minSdk=34`). No Gradle.
ADB WiFi device: `192.168.0.131:42113`.

## Prerequisites

Two env vars must be set before any command:
```bash
export ANDROID_JAR=/data/data/com.termux/files/home/android-sdk/platforms/android-34/android.jar
export ANDROID_NDK_HOME=/data/data/com.termux/files/home/android-sdk/ndk/26.1.10909125
```

The `~/AI/arm64-ndk/arm64-android.toolchain.cmake` toolchain must be present (sets Clang compiler to native ARM64, `--target=aarch64-linux-android34`, and NDK sysroot). If `adb` can't connect: `adb connect 192.168.0.131:42113`.

## Build & Package (7-step sequence, order matters)

Run all from project root inside PRoot/Termux.

```bash
# 1. Compile shaders to assets/shaders/  (standalone; NOT wired into CMake)
./scripts/compile_shaders.sh

# 2. Configure — ALWAYS use ndk-build/ as the source dir, NOT the root CMakeLists.txt
cmake -S ndk-build -B build \
  -DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Debug -G Ninja

# 3. Build .so
ninja -C build          # → build/libmobilecad.so

# 4. Generate classes.dex (MUST be done before packaging; hasCode=true requires it)
rm -rf /tmp/mcad-java /tmp/mcad-classes /tmp/mcad-dex
mkdir -p /tmp/mcad-java /tmp/mcad-classes /tmp/mcad-dex
echo 'package com.mobilecad; public class R {}' > /tmp/mcad-java/R.java
echo 'package com.mobilecad; public class BuildConfig {}' > /tmp/mcad-java/BuildConfig.java
javac -source 8 -target 8 -bootclasspath "$ANDROID_JAR" -d /tmp/mcad-classes /tmp/mcad-java/*.java 2>/dev/null
/data/data/com.termux/files/home/android-sdk/build-tools/34.0.0/d8 \
  --lib "$ANDROID_JAR" --output /tmp/mcad-dex /tmp/mcad-classes/com/mobilecad/*.class
cp /tmp/mcad-dex/classes.dex staging/

# 5. Package APK (staging/ is ephemeral — rm -rf and recreate every time)
rm -rf staging && mkdir -p staging/lib/arm64-v8a
cp build/libmobilecad.so staging/lib/arm64-v8a/
cp "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" staging/lib/arm64-v8a/
cp -r assets staging/
cp /tmp/mcad-dex/classes.dex staging/
# staging/AndroidManifest.xml is tracked in the repo and is copied from staging/ by the build pipeline
aapt2 link -I "$ANDROID_JAR" --manifest staging/AndroidManifest.xml -A staging/assets -o mobilecad-base.apk
cd staging && zip -ur ../mobilecad-base.apk lib/arm64-v8a/ && cd ..
zipalign -v 4 mobilecad-base.apk mobilecad-aligned.apk
apksigner sign --ks debug.keystore --ks-pass pass:android --key-pass pass:android \
  --ks-key-alias androiddebugkey mobilecad-aligned.apk
apksigner verify mobilecad-aligned.apk

# 6. Install + launch
adb install -r mobilecad-aligned.apk
adb shell "am force-stop com.mobilecad; am start -n com.mobilecad/android.app.NativeActivity"

# 7. Verify
adb logcat -v threadtime | grep MobileCAD:
```

Key logcat milestones (in order):
- `android_main ENTERED` — NDK lifecycle working
- `CADApp::init: starting` → `CADApp::init: done` — Vulkan init complete
- `onWindowResized ENTRY` — swapchain rebuild triggered (fires on initial window creation too)
- `drawFrame step-5: acquire result=0` — swapchain image acquired each frame
- Final: `android_main EXITING  (drawFrame ran N times)` — clean shutdown

## Critical quirks (these are easy to get wrong)

**`ndk-build/CMakeLists.txt` is the only correct CMake entry point.** The root `CMakeLists.txt` only defines a `compile_shaders` shim target with `-Werror` — it has no NDK/linker flags. The `android/` Gradle path is stale (`android/app/src/main/cpp/` doesn't exist; its `AndroidManifest.xml` has `hasCode="false"`).

**`android_main` signature must be exact** — Use `extern "C" __attribute__((visibility("default")))` on the symbol (`src/main.cpp:125`). Missing either → installs and launches but never calls your code (black screen, no logcat output).

**NDK r26 removed `pendingCommand` / `android_app_clear_pending_cmd`.** The event loop must use `ALooper_pollOnce()` + `source->process(state, source)` (`src/main.cpp:145-153`).

**`android:hasCode="false"` kills the process before `android_main` fires.** On this Android 14 build the zygote never spawns. The `staging/AndroidManifest.xml` already uses `hasCode="true"`, but a real `classes.dex` must exist in the APK (step 4 above generates it from stubs).

**`cleanupSwapchain()` must be called before `m_vkContext.reset()`.** `cleanupSwapchain()` reaches `m_vkContext->getDevice()` — resetting context first is a null-pointer dereference. The first `APP_CMD_WINDOW_RESIZED` fires on initial window creation, so the wrong order crashes immediately after `CADApp::init: done`. This is confirmed by the incident reported in `BUILD_FIXES.md` §8. The correct code now calls `cleanupSwapchain()` → `m_swapchain.reset()` → `m_vkContext.reset()` (CADApp.cpp:261–328).

**`-lgcc` is absent from the NDK sysroot.** Built into `ndk-build/CMakeLists.txt:94-99`: `-rtlib=compiler-rt`, `-unwindlib=libunwind`, `-resource-dir`, and `-L` paths for Clang 17 builtins and libunwind.

**Shader compilation is a separate step.** `ndk-build/CMakeLists.txt` has no shader target. Run `./scripts/compile_shaders.sh` standalone; output lands in `shaders/compiled/`. The APK package step copies from `assets/shaders/`.

**Slang submodule must be initialised first** — `vk_video` headers (AV1, VP9) are copied from `external/slang/build/_deps/vulkan_headers-src/include/vk_video/` by `ndk-build/CMakeLists.txt:28-36`. Run `git submodule update --init --recursive` in `external/slang/` before configure.

**`aapt2 link -A` does not bundle `.so` files.** Add `lib/arm64-v8a/` after `aapt2 link` with `zip -ur`. Must include both `libmobilecad.so` and `libc++_shared.so`.

## Source layout

```
src/
├── main.cpp                                  # NDK entry — android_main at line 125
├── app/CADApp.{h,cpp}                        # Rendering orchestrator (init/drawFrame/onWindowResized/cleanup)
├── rendering/
│   ├── VulkanContext.{h,cpp}                 # VkInstance, device, surface, command pool, sync ring (2 frames, kMaxFrames=2)
│   ├── Swapchain.{h,cpp}                     # VkSwapchainKHR (recreated on resize)
│   ├── Pipeline.{h,cpp}                      # SPIR-V module loading from APK assets or fallback filesystem
│   └── shaders/*.slang                        # Slang shader sources
└── utils/math/{Vec3,Mat4}.h                  # No glm dependency

ndk-build/CMakeLists.txt                       # ★ Primary CMake entry (vk_video copy, compiler-rt flags)
staging/AndroidManifest.xml                    # APK manifest template (tracked; hasCode=true, Vulkan 1.3)
assets/shaders/*.spv                           # Bundled at package time

android/                                       # BROKEN: do NOT use (stale Gradle paths, hasCode=false)
```

## Code conventions

- **Language / flags**: C++20, `-Wall -Wextra -Wpedantic` (root also adds `-Werror`)
- **Style**: Allman braces, 4-space indent, left pointer alignment (`int* p`)
- **Namespaces**: `app::`, `rendering::`, `utils::`
- **Naming**: `PascalCase` types/files, `camelCase` locals/members, `snake_case` free functions, `m_` prefix on members
- **Ownership**: `std::unique_ptr` everywhere; Vulkan handles are raw `Vk*` managed by RAII classes; copy/move deleted on RAII types
- **Sync ring**: 2 frames max — `m_waitSemaphores[2]`, `m_signalSemaphores[2]`, `m_fences[2]`, `m_cmdBuffers` (vector) — `kMaxFrames = 2` in VulkanContext.h; some Adreno (Turnip) drivers crash on `vkQueueSubmit` with >4 in-flight
- **Re-format before editing**: `clang-format -i src/**/*.cpp src/**/*.h` (`.clang-format`: Google / Allman / 4-space / left pointer / 100-col / sorted includes)

## Scripts reference

| Script | What it does |
|--------|-------------|
| `scripts/compile_shaders.sh` | Runs `slangc` over `src/rendering/shaders/*.slang` → `shaders/compiled/*.spv` |
| `scripts/build.sh` | Desktop host build (no toolchain; not for ARM64 APK) |
| `scripts/build_slang.sh` | Builds Slang from source (interactive, prompts for Enter key) |
| `scripts/setup_android.sh` | Termux package install (clang, cmake, ninja, vulkan-tools) |

## Related docs
- `CLAUDE.md` — Architecture overview, NDK lifecycle, Vulkan object lifetimes, C++ Core Guidelines
- `BUILD_FIXES.md` — Root-cause history of every build issue encountered (15 numbered sections)
- `MOBILECAD_CONTEXT.md` — Session context, verified state, git log

> `AGENTS.md` covers the how-to-build facts agents actually miss. Architecture details, testing status, and deeper build diagnostics live in the linked files.
