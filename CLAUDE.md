# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MobileCAD is a native Android CAD application built with C++20 and Vulkan using Android NativeActivity. No X11, no GLFW — the app runs directly on Android via `ANativeWindow` and the NDK's `android_native_app_glue` library. Shaders are written in Slang and compiled to SPIR-V at build time on the host.

**Current State**: The project's core Vulkan rendering pipeline works — `android_main` is entered, VulkanContext initializes (instance → surface → physical device → logical device → command pool → sync objects → swapchain → render pass → pipeline → framebuffers), and `drawFrame()` runs each frame. CADApp.cpp was corrupted by an edit-drift bug that spliced `drawFrame()`'s body inside `initFramebuffers()`; it was restored from git. Two launch-kill bugs were fixed: `hasCode="false"` on the manifest (requires `classes.dex` + `hasCode=true`), and `onWindowResized()` cleanup ordering. The colored-triangle background-building demo is verified-running through logcat; screen output not yet confirmed on device.

## Tech Stack

- **Graphics**: Vulkan 1.2+ (via vulkan.hpp C++ bindings)
- **Windowing/Input**: Android NDK + NativeActivity + ANativeWindow
- **Shader Language**: Slang (.slang → .spv, compiled on host)
- **Build System**: CMake 3.20+ + Ninja for native code, `package_apk.sh` (custom) for APK packaging — no Gradle
- **NDK**: r26+ with Clang toolchain, android_native_app_glue
- **Language**: C++20
- **Math**: Custom Vec3, Mat4 (glm-like but minimal)

## Architecture Support

- **Primary target**: ARM64 (arm64-v8a, Android 14+)
- The code is architecture-agnostic; uses only standard C++20, Vulkan, and NDK

## Codebase Organization

```
MobileCAD/
├── CMakeLists.txt              # Root build config (shader compilation)
├── src/
│   ├── main.cpp                # NDK entry point (android_main + NativeActivity glue)
│   ├── app/
│   │   └── CADApp.h/cpp       # Main application class, orchestrates rendering
│   ├── rendering/
│   │   ├── VulkanContext.h/cpp    # Vulkan: instance, device, Android surface, command pool, sync objects
│   │   ├── Swapchain.h/cpp        # Swapchain creation & management
│   │   ├── Pipeline.h/cpp         # Shader module loading utilities
│   │   └── shaders/
│   │       ├── simple.vert.slang  # Vertex shader (pass-through with color)
│   │       └── simple.frag.slang  # Fragment shader (solid color output)
│   └── utils/
│       ├── math/
│       │   ├── Vec3.h         # 3D vector (float[3])
│       │   └── Mat4.h         # 4x4 matrix (float[4][4])
│       └── Result.h           # Expected-like result type for error handling
├── android/                        # Android NDK project
│   ├── build.gradle.kts            # Root Gradle config
│   ├── settings.gradle.kts         # Project settings
│   ├── gradle.properties           # Gradle/NDK properties
│   ├── gradle/wrapper/             # Gradle wrapper
│   └── app/
│       ├── CMakeLists.txt          # NDK CMake config (links android, log, native_app_glue, vulkan)
│       └── src/main/
│           ├── AndroidManifest.xml # NativeActivity declaration, Vulkan feature flags
│           └── AndroidManifest.xml # App configuration
├── shaders/compiled/              # Generated SPIR-V files (gitignored)
├── scripts/
│   ├── compile_shaders.sh     # Standalone shader compilation (host)
│   └── build_slang.sh         # Build Slang from source (host)
├── README.md                  # User-facing documentation
└── CLAUDE.md                  # This file
```

**Namespace Organization:**
- `app::` - High-level application logic (CADApp)
- `rendering::` - All Vulkan-related classes (VulkanContext, Swapchain, Pipeline)
- `utils::` - Utilities (Result<T>, math types)

**Memory Management**: RAII everywhere. Vulkan handles are raw Vk* types but managed by classes with explicit destroy in destructor. C++ objects use `std::unique_ptr` for ownership. Sync objects are ring-buffered (8 frames max).

## Development Environment

This project is developed in an **Ubuntu environment running in PRoot** (via `proot-distro`) on the **Termux** app, on a **Samsung Galaxy S23 Ultra** smartphone. All compilation happens directly on the device — no cross-compilation from a desktop machine.

- **Host**: PRoot Ubuntu on Termux (Samsung Galaxy S23 Ultra, ARM64/aarch64)
- **Compiler**: Clang (native aarch64-linux-android)
- **Result binaries**: Native ARM64 Android binaries (`mobilecad.so` loaded via Android NDK)
- **Deployment**: Direct APK install via `adb` from the same device (Termux runs `adb` pointing to localhost via `adb connect localhost:5555` when wireless debugging is enabled)

## Development Setup

### Prerequisites

- **Android NDK**: r26+ (includes Vulkan headers, android_native_app_glue)
- **Android SDK**: Command-line tools, build-tools, platform-android-34 (for `android.jar`)
- **Build**: CMake 3.20+, Ninja
- **APK tooling**: `aapt2`, `zipalign`, `apksigner`, `keytool` — all installable via `apt` in PRoot Ubuntu
- **Toolchain**: `~/AI/arm64-ndk/arm64-android.toolchain.cmake` (ARM64 hybrid toolchain, see below)
- **Shaders**: `slangc` installed in PRoot Ubuntu (compiles `.slang` → `.spv` on-device)

> **No Gradle required.** Gradle's NDK plugin invokes x86_64 host tools from the NDK, which cannot
> run natively in this ARM64 PRoot environment. The custom toolchain and `package_apk.sh` replace
> Gradle entirely, using the system's native ARM64 clang as the compiler and borrowing only the
> NDK's sysroot, headers, and libraries.

### Quick Start

```bash
# 1. Compile shaders (run from project root, inside PRoot)
./scripts/compile_shaders.sh

# 2. Configure — point CMake at the ARM64 hybrid toolchain
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake \
  -G Ninja

# 3. Build the shared library
ninja -C build

# 4. Package into an APK (no Gradle)
~/AI/arm64-ndk/package_apk.sh \
  --so build/libmobilecad.so \
  --manifest AndroidManifest.xml \
  --assets assets/ \
  --out mobilecad-debug.apk

# 5. Install on device (from Termux, outside PRoot)
adb install -r mobilecad-debug.apk

# 6. Launch
adb shell "am start -n com.mobilecad/android.app.NativeActivity"

# 7. View logs
adb logcat MobileCAD:* *:S
```

> **Why not Gradle?** The official NDK r26 ships x86_64 host tools (`clang`, `lld`, etc.) that
> cannot execute natively in this ARM64 PRoot environment. Instead, the build uses a hybrid
> approach: the system's native ARM64 clang as the compiler/linker, and the NDK purely for its
> sysroot, headers, and `android_native_app_glue`. `package_apk.sh` replicates what Gradle's
> NDK plugin does (resource linking, `.so` bundling, alignment, signing) using native ARM64 tools.

### Shader Compilation

Shaders are compiled **on-device inside PRoot** using `slangc` (native ARM64), then copied to `assets/shaders/` before APK packaging:

```bash
# Compile all .slang shaders to .spv (run from project root inside PRoot)
./scripts/compile_shaders.sh

# Compiled .spv files land in assets/shaders/
ls assets/shaders/*.spv
```

### APK Packaging

The build uses a manual `staging/`-based packaging pipeline. `classes.dex` must also be generated from stub Java classes (see `MOBILECAD_CONTEXT.md` for the exact one-liner). See `BUILD_FIXES.md` §7 and §10 for the complete sequence.

> **No Gradle required.**


### Linting & Formatting

```bash
# Format all C++ files
clang-format -i src/**/*.cpp src/**/*.h

# Check formatting (dry-run)
clang-format --dry-run --Werror src/**/*.cpp src/**/*.h
```

**Note**: `.clang-format` uses Google style with Allman braces, 4-space indent, left pointer alignment.

### Debugging

- **Debug build**: Compile with debug flags by passing `-DCMAKE_BUILD_TYPE=Debug` to CMake:
  ```bash
  cmake -B build \
    -DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -G Ninja
  ```
  Debug builds include:
  - NDK debug symbols (`-g`)
  - Vulkan validation layers (if installed on device)
  - Debug Utils extension with callback (warnings/errors to logcat)
  - `assert()` checks enabled

- **Vulkan validation**: Check logcat output. Look for "MobileCAD" tagged messages:
  ```bash
  adb logcat MobileCAD:* AndroidRuntime:* *:E
  ```

- **RenderDoc**: Can capture frames on the device:
  1. Install RenderDoc on desktop
  2. Connect to device via ADB
  3. Capture frames from the running APK

- **Common issues**:
  - "Failed to create Android surface" → `vkCreateAndroidSurfaceKHR` failed; check ANativeWindow valid
  - Black screen → shaders not bundled in APK; check `android/app/src/main/assets/shaders/`

## Architecture Guide

### High-Level Flow

1. **main.cpp** (`android_main`): NDK entry point. Sets up `android_app` callbacks (`onAppCmd`, `onInputEvent`). Polls events via `ALooper_pollAll`. Calls `CADApp::drawFrame()` each frame.

2. **CADApp**:
   - Constructor: receives `ANativeWindow*` from NDK → creates `VulkanContext` → initializes swapchain, render pass, pipeline, framebuffers
   - `drawFrame()`: acquires swapchain, records commands into ring-buffered command buffer, submits with fences, presents
   - `onWindowResized()`: recreates VulkanContext, swapchain, framebuffers on window change
   - Cleanup: destructor calls `cleanup()` (reverse order of creation)

3. **VulkanContext**: Creates Vulkan instance with `VK_KHR_android_surface`, creates surface via `vkCreateAndroidSurfaceKHR`, selects GPU, creates logical device, command pool, ring-buffered sync objects (semaphores + fences for 8 frames). Singleton-like (one per app).

4. **Swapchain**: Wraps `VkSwapchainKHR`, manages image acquisition and cleanup. Recreated on resize.

5. **Pipeline**: Static utilities to load SPIR-V and create shader modules.

### NDK Lifecycle Integration

- **APP_CMD_INIT_WINDOW** → Create CADApp (initializes Vulkan)
- **APP_CMD_TERM_WINDOW** → Cleanup and destroy CADApp
- **APP_CMD_WINDOW_RESIZED** → Call `CADApp::onWindowResized()` → recreate swapchain
- **APP_CMD_DESTROY** → Full cleanup, destroy CADApp
- **Back button (AKEYCODE_BACK)** → Sets `g_running = false`, loop exits, app terminates

### Vulkan Object Lifetimes (RAII)

- `VulkanContext`: Owns `VkInstance`, `VkDevice`, `VkPhysicalDevice`, `VkSurfaceKHR`, `VkCommandPool`, ring-buffered `VkSemaphore[8]`, `VkFence[8]`, `VkCommandBuffer[8]`, `VkDebugUtilsMessengerEXT`. Destroyed in destructor.
- `Swapchain`: Owns `VkSwapchainKHR` and `VkImageView[]`. Recreated on resize.
- `CADApp` owns: `VkRenderPass`, `VkPipeline`, `VkPipelineLayout`, `VkDescriptorSetLayout`, `VkFramebuffer[]`, vertex/index buffers.
- Rule: Destruction order must reverse creation order (child before parent).

### Error Handling Patterns

- **Initialization failures**: Throw `std::runtime_error` with descriptive message.
- **Runtime errors**: Log to logcat via `__android_log_print`, throw or handle recoverably.
- **`utils::Result<T>`**: Result type exists for future non-fatal error propagation.

### C++ Core Guidelines Compliance

- **No raw pointers for ownership**: Use `std::unique_ptr`. Raw pointers only for non-owning references.
- **`noexcept`**: Inconsistent; not yet systematically applied.
- **Rule of Zero**: Not fully followed (Vulkan wrapper classes define destructors, copy/move deleted). Acceptable for RAII types managing resources.
- **Initialize all members**: Class members initialize to `{nullptr}` or `{VK_NULL_HANDLE}`.

## Conventions

### Naming (Follow Existing Code)

- **Files**: `PascalCase.h/cpp` for classes
- **Types**: `PascalCase` (classes, structs, enum classes)
- **Functions**: `camelCase()` for member functions; `snake_case()` for free functions
- **Variables**: `camelCase` for locals/members, `m_` prefix for members
- **Namespaces**: `lowercase` (app, rendering, utils)
- **Macros**: `UPPER_SNAKE_CASE`

### Format

- **Brace style**: Allman (opening brace on new line)
- **Indent**: 4 spaces (no tabs)
- **Pointer alignment**: Left (`int* p`, not `int *p`)
- **Column limit**: 100
- **Include sorting**: Yes, by regex blocks (see `.clang-format`)

## Validation & Testing

### Running the Application

```bash
# 1. Compile shaders (run from project root, inside PRoot)
./scripts/compile_shaders.sh

# 2. Build .so and package APK (inside PRoot)
cmake -B build -DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake -G Ninja
ninja -C build

# 3. One-line DEX + full packaging (see MOBILECAD_CONTEXT.md for details)
~/AI/arm64-ndk/package_apk.sh \
  --so build/libmobilecad.so \
  --manifest staging/AndroidManifest.xml \
  --assets assets/ \
  --out mobilecad-debug.apk

# Install and launch (from Termux, outside PRoot)
adb install -r mobilecad-debug.apk
adb shell "am start -n com.mobilecad/android.app.NativeActivity"

# Monitor logs
adb logcat MobileCAD:* *:S
```

### Quick Verification Checklist

After installation, confirm:
- [ ] App launches and shows colored triangle on screen
- [ ] No Vulkan errors in logcat
- [ ] Back button exits app cleanly
- [ ] Device rotation triggers swapchain recreation

### Known Symptoms & Fixes

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| App crashes on launch | Vulkan not supported on device or shaders missing | Check device Vulkan version, ensure assets present |
| Black screen | Shaders not bundled | Re-run `compile_shaders.sh`, check `assets/shaders/*.spv` exist, re-run `package_apk.sh` |
| "Failed to create Android surface" | `ANativeWindow` invalid | Check NativeActivity lifecycle, ensure `APP_CMD_INIT_WINDOW` received |
| Validation errors | Vulkan validation layer issues | Check logcat, fix VUID violations |

## Testing Status

**No test infrastructure exists yet**. When implementing tests:
- Use GoogleTest via NDK
- Place tests in `tests/` directory
- Add `enable_testing()` to CMake, create test executable

## Known Issues & TODOs

- Pipeline creation logic is inline in CADApp; should be refactored into rendering::Pipeline.
- No input handling beyond back button. Touch gestures, stylus support needed.
- No geometry subsystem - hardcoded triangle vertices. CAD operations (boolean, transforms, measurements) not implemented.
- No UI system - immediate mode (ImGui) or retained-mode Vulkan UI needed.
- No file persistence - PostgreSQL/SQLite integration not started.
- Shader hot-reload not implemented.
- Vulkan memory management doesn't use Allocation Callback; no memory budgeting.
- No x86_64 support yet (only arm64-v8a).

## Build Commands Reference

```bash
# Compile shaders (inside PRoot)
./scripts/compile_shaders.sh

# Configure — debug build
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -G Ninja

# Configure — release build
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -G Ninja

# Build shared library
ninja -C build

# Clean build
rm -rf build/

# Package APK
~/AI/arm64-ndk/package_apk.sh \
  --so build/libmobilecad.so \
  --manifest staging/AndroidManifest.xml \
  --assets assets/ \
  --out mobilecad-debug.apk

# Install APK (from Termux, outside PRoot)
adb install -r mobilecad-debug.apk

# View logs
adb logcat MobileCAD:* *:S

# Uninstall
adb uninstall com.mobilecad
```

### CMake Details

`CMakeLists.txt` (project root):
- Always invoked with `-DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake`
- The toolchain sets the compiler to `/usr/bin/clang++` (native ARM64), the sysroot to the NDK
  `linux-x86_64/sysroot`, and injects `--target=aarch64-linux-android34` into all flags
- Builds `libmobilecad.so` as a shared library (loaded by NativeActivity at runtime)
- Links: `android_native_app_glue`, `android`, `log`, `vulkan`, `c++`, `c++abi`

Build types (passed via `-DCMAKE_BUILD_TYPE=`):
- **Debug**: `-g`, debug symbols, asserts enabled
- **Release**: `-O3 -DNDEBUG`, no debug overhead

Shader compilation: `scripts/compile_shaders.sh` runs `slangc` (native ARM64 in PRoot) to compile
`.slang` → `.spv`. Output goes to `assets/shaders/`. `package_apk.sh` then bundles the entire
`assets/` directory into the APK. Shaders are **not** compiled at CMake configure time.

### Dependencies & Version Requirements

- **Android NDK**: r26+
- **Android SDK**: minSdk 34 (Android 14), targetSdk 34, `android.jar` from `platforms/android-34`
- **Vulkan**: 1.4 on device (Turnip/Adreno 740 confirmed)
- **CMake**: 3.20+
- **Ninja**: any recent version
- **Slang**: installed in PRoot Ubuntu via `apt` or built from source (`slangc` on PATH)
- **APK tools**: `aapt2`, `zipalign`, `apksigner`, `keytool` (all via `apt` in PRoot Ubuntu)
- **Toolchain**: `~/AI/arm64-ndk/arm64-android.toolchain.cmake` + `~/AI/arm64-ndk/package_apk.sh`
