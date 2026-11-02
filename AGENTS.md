# AGENTS.md — MobileCAD

C++20 / Vulkan Android NDK app. ARM64-only target (`arm64-v8a`, `minSdk=34`). No Gradle.
ADB WiFi device: Dynamic pairing/connect port (e.g., `192.168.0.131:<PORT>`). **Never hardcode ports**; always query via `adb devices` or ask/query at session start (`adb connect 192.168.0.131:<PORT>`).

## Prerequisites

Two env vars must be set before any command:
```bash
export ANDROID_JAR=/data/data/com.termux/files/home/android-sdk/platforms/android-34/android.jar
export ANDROID_NDK_HOME=/data/data/com.termux/files/home/android-sdk/ndk/26.1.10909125
```

The `~/AI/arm64-ndk/arm64-android.toolchain.cmake` toolchain must be present (sets Clang compiler to native ARM64, `--target=aarch64-linux-android34`, and NDK sysroot).

## Automated Build & Deployment

The entire build, 16 KB ELF alignment, packaging, signing, and auto-installation is encapsulated in a single script:
```bash
./scripts/build_apk.sh
```

## Manual Build & Package Sequence (for reference)

Run all from project root inside Termux.

```bash
# 1. Compile shaders to assets/shaders/  (standalone; NOT wired into CMake)
./scripts/compile_shaders.sh

# 2. Configure — ALWAYS use ndk-build/ as the source dir, NOT the root CMakeLists.txt
cmake -S ndk-build -B build \
  -DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Debug -G Ninja

# 3. Build .so (with -Wl,-z,max-page-size=16384)
ninja -C build          # → build/libmobilecad.so

# 4. Generate classes.dex (MUST be done before packaging; hasCode=true requires it)
rm -rf /tmp/mcad-java /tmp/mcad-classes /tmp/mcad-dex
mkdir -p /tmp/mcad-java /tmp/mcad-classes /tmp/mcad-dex
echo 'package com.mobilecad; public class R {}' > /tmp/mcad-java/R.java
echo 'package com.mobilecad; public class BuildConfig {}' > /tmp/mcad-java/BuildConfig.java
javac -source 8 -target 8 -bootclasspath "$ANDROID_JAR" -d /tmp/mcad-classes /tmp/mcad-java/*.java 2>/dev/null
/data/data/com.termux/files/home/android-sdk/build-tools/34.0.0/d8 \
  --lib "$ANDROID_JAR" --output /tmp/mcad-dex /tmp/mcad-classes/com/mobilecad/*.class

# 5. Package APK (staging/ is ephemeral — rm -rf and recreate every time)
rm -rf staging && mkdir -p staging/lib/arm64-v8a staging/assets
cp build/libmobilecad.so staging/lib/arm64-v8a/
# Note: libc++_shared.so must also be 16 KB aligned
cp build/mcad-tmp/libc++_shared.so staging/lib/arm64-v8a/
cp -r assets/* staging/assets/
cp /tmp/mcad-dex/classes.dex staging/
# ndk-build/AndroidManifest.xml is the canonical manifest tracked in the repo
aapt2 link -I "$ANDROID_JAR" --manifest ndk-build/AndroidManifest.xml -A staging/assets -o mobilecad-base.apk
cd staging && zip -ur ../mobilecad-base.apk classes.dex && zip -0 -ur ../mobilecad-base.apk lib/arm64-v8a/ && cd ..
zipalign -v -P 16 4 mobilecad-base.apk mobilecad-aligned.apk
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
- Clean interactive frame loops without per-frame log spam
- Final: `android_main EXITING` — clean shutdown

## Critical quirks (these are easy to get wrong)

**`ndk-build/CMakeLists.txt` is the only correct CMake entry point.** The root `CMakeLists.txt` only defines a `compile_shaders` shim target. Gradle was completely purged; `ndk-build/` is the sole native build configuration.

**`android_main` signature must be exact** — Use `extern "C" __attribute__((visibility("default")))` on the symbol (`src/main.cpp:125`). Missing either → installs and launches but never calls your code (black screen, no logcat output).

**NDK r26 removed `pendingCommand` / `android_app_clear_pending_cmd`.** The event loop must use `ALooper_pollOnce()` + `source->process(state, source)` (`src/main.cpp`).

**`android:hasCode="false"` kills the process before `android_main` fires.** On Android 14/15 the zygote never spawns. The `ndk-build/AndroidManifest.xml` uses `hasCode="true"`, and a real `classes.dex` exists in the APK (generated from stubs).

**`cleanupSwapchain()` must be called before `m_vkContext.reset()`.** `cleanupSwapchain()` reaches `m_vkContext->getDevice()` — resetting context first is a null-pointer dereference. Order must be: `vkDeviceWaitIdle()` → `cleanupSwapchain()` → `m_swapchain.reset()` → `m_vkContext.reset()` (`CADApp.cpp`).

**`-lgcc` is absent from the NDK sysroot.** Built into `ndk-build/CMakeLists.txt`: `-rtlib=compiler-rt`, `-unwindlib=libunwind`, `-resource-dir`, and `-L` paths for Clang 17 builtins and libunwind.

**Shader compilation is a separate step.** `ndk-build/CMakeLists.txt` has no shader target. Run `./scripts/compile_shaders.sh` or `./scripts/build_apk.sh`; output lands in `shaders/compiled/` and is bundled from `assets/shaders/`.

**Slang submodule must be initialised first** — `vk_video` headers (AV1, VP9) are copied from `external/slang/build/_deps/vulkan_headers-src/include/vk_video/` by `ndk-build/CMakeLists.txt`. Run `git submodule update --init --recursive` in `external/slang/` before configure.

**`aapt2 link -A` does not bundle `.so` files.** Add `lib/arm64-v8a/` after `aapt2 link` with `zip -0 -ur` (uncompressed for 16 KB mmap alignment). Must include both `libmobilecad.so` and `libc++_shared.so`.

## Source layout

```
src/
├── main.cpp                                  # NDK entry — android_main
├── app/CADApp.{h,cpp}                        # Rendering orchestrator (init/drawFrame/onWindowResized/cleanup)
├── rendering/
│   ├── VulkanContext.{h,cpp}                 # VkInstance, device, surface, command pool, sync ring (2 frames, kMaxFrames=2)
│   ├── Swapchain.{h,cpp}                     # VkSwapchainKHR & D32_SFLOAT depth buffer (recreated on resize)
│   ├── Pipeline.{h,cpp}                      # SPIR-V module loading from APK assets or fallback filesystem
│   ├── Camera.h                              # Spherical Arcball camera (orbit/pan/zoom, multi-touch & S-Pen)
│   └── shaders/*.slang                        # Slang shader sources (simple.vert.slang, simple.frag.slang)
└── utils/math/{Vec3,Mat4}.h                  # Custom 3D vector & matrix math (Vulkan right-handed NDC)

ndk-build/
├── CMakeLists.txt                            # Primary CMake entry (vk_video copy, compiler-rt flags, 16KB alignment)
└── AndroidManifest.xml                       # Canonical APK manifest (hasCode=true, Vulkan 1.3/1.4)
assets/shaders/*.spv                           # Bundled at package time
scripts/build_apk.sh                          # Automated one-step build & package & install script
```

## Code conventions

- **Language / flags**: C++20, `-Wall -Wextra -Wpedantic`
- **Style**: Allman braces, 4-space indent, left pointer alignment (`int* p`)
- **Namespaces**: `app::`, `rendering::`, `utils::`
- **Naming**: `PascalCase` types/files, `camelCase` locals/members, `snake_case` free functions, `m_` prefix on members
- **Ownership**: `std::unique_ptr` everywhere; Vulkan handles are raw `Vk*` managed by RAII classes; copy/move deleted on RAII types
- **Sync ring**: 2 frames max — `m_waitSemaphores[2]`, `m_signalSemaphores[2]`, `m_fences[2]`, `m_cmdBuffers` (vector) — `kMaxFrames = 2` in VulkanContext.h
- **Re-format before editing**: `clang-format -i src/**/*.cpp src/**/*.h` (`.clang-format`: Google / Allman / 4-space / left pointer / 100-col / sorted includes)

## Scripts reference

| Script | What it does |
|--------|-------------|
| `scripts/build_apk.sh` | Automated end-to-end build, 16 KB realignment, packaging, signing, and auto-install |
| `scripts/compile_shaders.sh` | Runs `slangc` over `src/rendering/shaders/*.slang` → `shaders/compiled/*.spv` & `assets/shaders/` |
| `scripts/build_slang.sh` | Builds Slang from source (interactive, prompts for Enter key) |
| `scripts/setup_android.sh` | Termux native package install (clang, cmake, ninja, openjdk-17, aapt2, zipalign, apksigner, android-tools) |

## Related docs
- `CLAUDE.md` — Architecture overview, NDK lifecycle, Vulkan object lifetimes, C++ Core Guidelines
- `BUILD_FIXES.md` — Root-cause history of every build issue encountered (22 numbered sections)
- `MOBILECAD_CONTEXT.md` — Session context, verified state, git log

> `AGENTS.md` covers the how-to-build facts agents actually miss. Architecture details, testing status, and deeper build diagnostics live in the linked files.
