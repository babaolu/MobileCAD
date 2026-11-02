# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MobileCAD is a native Android CAD application built with C++20 and Vulkan 1.4 using Android NativeActivity. No X11, no GLFW, no Gradle — the app runs directly on Android via `ANativeWindow` and the NDK's `android_native_app_glue` library. Shaders are written in Slang and compiled to SPIR-V.

**Current State**: Full 3D rendering pipeline with hardware `VK_FORMAT_D32_SFLOAT` depth buffer, push constant MVP transformation matrix, and interactive Touch/S-Pen spherical Arcball camera (orbit, pan, pinch zoom) rendering a 3D CAD reference cube. 16 KB ELF page-size alignment is enforced and verified compliant on Android 15. The application builds and packages natively on-device in Termux via `scripts/build_apk.sh`.

## Tech Stack

- **Graphics**: Vulkan 1.4 / 1.3 (dynamic rendering & render passes, D32_SFLOAT depth buffer)
- **Windowing/Input**: Android NDK + NativeActivity + ANativeWindow (multi-touch & S-Pen stylus support)
- **Shader Language**: Slang (`.slang` → SPIR-V 1.3 `glsl_450` profile via `slangc`)
- **Build System**: CMake 3.20+ + Ninja (`ndk-build/CMakeLists.txt`), automated by `scripts/build_apk.sh` — no Gradle
- **NDK**: r26+ with Clang toolchain, android_native_app_glue
- **Language**: C++20 (`-std=c++20`, Allman style, RAII)
- **Math**: Custom `Vec3`, `Mat4` (Vulkan right-handed NDC: +Y down, [0, 1] depth)
- **Page Alignment**: Strict 16 KB (`-Wl,-z,max-page-size=16384`, `zipalign -P 16 4`)

## Architecture Support

- **Primary target**: ARM64 (`arm64-v8a`, Android 14+ / API 34+, tested on Galaxy S23 Ultra / Snapdragon 8 Gen 2 / Adreno 740)
- The code is architecture-agnostic; uses only standard C++20, Vulkan, and NDK

## Codebase Organization

```
MobileCAD/
├── ndk-build/
│   ├── CMakeLists.txt         # Primary NDK CMake config (compiler-rt, 16 KB alignment, libc++ paths)
│   └── AndroidManifest.xml    # Canonical APK manifest (hasCode=true, Vulkan 1.3/1.4, portrait)
├── src/
│   ├── main.cpp               # NDK entry point (android_main + event loop + touch/stylus routing)
│   ├── app/
│   │   ├── CADApp.h           # Main application class orchestrator
│   │   └── CADApp.cpp         # Init, resize, teardown, drawFrame, and procedural CAD cube
│   ├── rendering/
│   │   ├── VulkanContext.h/cpp # VkInstance, VkDevice, VkSurfaceKHR, VkQueue, sync ring (2 frames)
│   │   ├── Swapchain.h/cpp    # Swapchain management + D32_SFLOAT depth buffer & depth image view
│   │   ├── Pipeline.h/cpp     # SPIR-V shader module loading utilities (AAssetManager + fallback)
│   │   ├── Camera.h           # Spherical Arcball camera (orbit, pan, zoom, S-Pen & touch input)
│   │   └── shaders/
│   │       ├── simple.vert.slang # PushConstant MVP vertex shader (procedural cube)
│   │       └── simple.frag.slang # Per-vertex colored fragment shader
│   └── utils/
│       ├── math/
│       │   ├── Vec3.h         # 3D vector math (operations, cross, dot, normalize)
│       │   └── Mat4.h         # 4x4 matrix math (Vulkan perspective, lookAt, translate, scale)
│       └── Result.h           # Expected-like result type for error handling
├── assets/shaders/            # Precompiled SPIR-V binaries bundled into APK
├── scripts/
│   ├── build_apk.sh           # Automated end-to-end build, 16 KB realignment, package, sign, install
│   ├── compile_shaders.sh     # Standalone Slang shader compilation to SPIR-V 1.3
│   ├── build_slang.sh         # Build Slang compiler from source
│   └── setup_android.sh       # Native Termux environment dependency installer
├── README.md                  # Complete architectural & technical documentation
├── BUILD_FIXES.md             # Comprehensive log of root-cause fixes (22 sections)
├── AGENTS.md                  # Agent build instructions and critical operational rules
└── CLAUDE.md                  # This file
```

**Namespace Organization:**
- `app::` - High-level application logic (CADApp)
- `rendering::` - All Vulkan-related classes (VulkanContext, Swapchain, Pipeline)
- `utils::` - Utilities (Result<T>, math types)

**Memory Management**: RAII everywhere. Vulkan handles are raw Vk* types managed by RAII classes with explicit destroy in destructor. C++ objects use `std::unique_ptr` for ownership. Sync objects are ring-buffered (`kMaxFrames = 2`).

## Development Environment

This project is developed directly on a **Samsung Galaxy S23 Ultra** smartphone (ARM64, Snapdragon 8 Gen 2, Adreno 740, Android 14/15) inside **Termux**. All compilation, linking, packaging, and signing happen natively on the device — no cross-compilation from a desktop machine.

- **Host**: Termux on Samsung Galaxy S23 Ultra (`aarch64-linux-android`)
- **Compiler**: Clang 17+ (native ARM64 targeting `aarch64-linux-android34`)
- **Binary**: Native ARM64 shared library (`libmobilecad.so` loaded by `android.app.NativeActivity`)
- **Deployment**: Direct APK installation via `adb` (connecting over local Wi-Fi debugging with dynamic port)

## Development Setup

### Prerequisites

- **Android NDK**: r26+ (`/data/data/com.termux/files/home/android-sdk/ndk/26.1.10909125`)
- **Android SDK**: Build-tools 34.0.0, platform 34 (`android.jar`)
- **Build**: CMake 3.20+, Ninja
- **APK tooling**: `aapt2`, `zipalign`, `apksigner`, `keytool` (native Termux packages)
- **Toolchain**: `~/AI/arm64-ndk/arm64-android.toolchain.cmake`
- **Shaders**: `slangc` (native ARM64 binary or run via PRoot Ubuntu)

> **No Gradle required.** The build pipeline completely replaces Gradle with `ndk-build/CMakeLists.txt` and `scripts/build_apk.sh`, ensuring fast on-device builds with strict 16 KB page-size compliance.

### Quick Start (One Command)

```bash
# Automated end-to-end build, 16 KB realignment, package, sign, and install:
./scripts/build_apk.sh
```

### Manual Step-by-Step Build

```bash
# 1. Compile Slang shaders to SPIR-V 1.3
./scripts/compile_shaders.sh

# 2. Configure native build using ndk-build/
cmake -S ndk-build -B build \
  -DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Debug -G Ninja

# 3. Build libmobilecad.so (16 KB aligned)
ninja -C build

# 4. Re-link 16 KB-aligned libc++_shared.so and compile Java stub classes.dex
# (Automated inside scripts/build_apk.sh)

# 5. Package, align (16 KB -P 16 4), and sign
# (Automated inside scripts/build_apk.sh)
```

### Shader Compilation

Shaders are authored in Slang in `src/rendering/shaders/` and compiled to SPIR-V 1.3 with a `glsl_450` profile (to avoid Adreno driver bugs with push constant parameters in `OpEntryPoint`):

```bash
./scripts/compile_shaders.sh
```

Compiled `.spv` binaries land in `shaders/compiled/` and are copied to `assets/shaders/` to be bundled into the APK.

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
  - Black screen → shaders not bundled in APK; check `assets/shaders/`
  - Linker failure on Adreno → check SPIR-V version (use SPIR-V 1.3 `glsl_450` profile in `compile_shaders.sh`)

## Architecture Guide

### High-Level Flow

1. **main.cpp** (`android_main`): NDK entry point. Sets up `android_app` callbacks (`onAppCmd`, `onInputEvent`). Dispatches touch and S-Pen stylus events to `CADApp::onInputEvent()`. Polls events via `ALooper_pollOnce`. Calls `CADApp::drawFrame()` each frame.

2. **CADApp**:
   - Constructor: receives `ANativeWindow*` from NDK → creates `VulkanContext` → initializes `Swapchain` (color + D32_SFLOAT depth buffer), render pass, pipeline, framebuffers, and `Camera`.
   - `drawFrame()`: acquires swapchain, updates PushConstant MVP from `Camera::getViewProjection()`, records commands into ring-buffered command buffer, submits with fences, and presents.
   - `onInputEvent()`: processes `AMOTION_EVENT_ACTION_*` for single-finger orbit, two-finger pan & pinch zoom, and S-Pen stylus hovering/drawing.
   - `onWindowResized()`: recreates swapchain and framebuffers on window resize or rotation.
   - Cleanup: destructor calls `cleanup()` (`vkDeviceWaitIdle()` → `cleanupSwapchain()` → `m_swapchain.reset()` → `m_vkContext.reset()`).

3. **VulkanContext**: Creates Vulkan instance with `VK_KHR_android_surface`, creates surface via `vkCreateAndroidSurfaceKHR`, selects physical device, creates logical device, command pool, and ring-buffered sync objects (`kMaxFrames = 2`).

4. **Swapchain**: Wraps `VkSwapchainKHR`, manages image acquisition, and allocates a dedicated `VK_FORMAT_D32_SFLOAT` hardware depth image and image view. Recreated cleanly on resize.

5. **Camera**: Spherical Arcball camera managing yaw, pitch, distance, and 3D target position. Computes right-handed Vulkan view and projection matrices (+Y down, [0, 1] depth).

6. **Pipeline**: Loads SPIR-V from APK assets via `AAssetManager` with filesystem fallback, and creates shader modules.

### NDK Lifecycle Integration

- **APP_CMD_INIT_WINDOW** → Create `CADApp` (initializes Vulkan)
- **APP_CMD_TERM_WINDOW** → Cleanup and destroy `CADApp`
- **APP_CMD_WINDOW_RESIZED** → Call `CADApp::onWindowResized()` → recreate swapchain
- **APP_CMD_DESTROY** → Full cleanup, destroy `CADApp`
- **Back button (AKEYCODE_BACK)** → Sets `g_running = false`, event loop exits, app terminates cleanly

### Vulkan Object Lifetimes (RAII)

- `VulkanContext`: Owns `VkInstance`, `VkDevice`, `VkPhysicalDevice`, `VkSurfaceKHR`, `VkCommandPool`, ring-buffered `VkSemaphore[2]`, `VkFence[2]`, `VkCommandBuffer[2]`, `VkDebugUtilsMessengerEXT`. Destroyed in destructor.
- `Swapchain`: Owns `VkSwapchainKHR`, `VkImageView[]` (color), and `VkImage` + `VkImageView` (depth). Recreated on resize.
- `CADApp` owns: `VkRenderPass`, `VkPipeline`, `VkPipelineLayout`, `VkDescriptorSetLayout`, `VkFramebuffer[]`, `Camera`.
- **Teardown Rule**: Destruction order must strictly reverse creation order:
  `vkDeviceWaitIdle()` → `cleanupSwapchain()` → `m_swapchain.reset()` → `m_vkContext.reset()`.

## Validation & Testing

### Running the Application

```bash
# Automated end-to-end build and install:
./scripts/build_apk.sh

# Monitor logs:
adb logcat MobileCAD:* *:S
```

### Quick Verification Checklist

After installation, confirm:
- [ ] App launches and displays the 3D colored CAD reference cube on screen
- [ ] Depth testing works properly (no back faces showing through front faces)
- [ ] Single touch drag orbits the camera smoothly around the cube
- [ ] Two-finger drag pans the camera; pinch gesture zooms in/out
- [ ] S-Pen stylus interactions function with high precision
- [ ] Device rotation triggers seamless swapchain recreation
- [ ] Back button exits the app cleanly with zero memory leaks

## Current Status & Next Roadmap Phases

- **Phase 0–8**: Complete. Vulkan 1.4 initialization, swapchain recreation, dynamic viewport/scissor, hardware D32_SFLOAT depth buffer, PushConstant MVP, procedural CAD reference cube, spherical Arcball camera (touch & S-Pen), 16 KB ELF alignment compliance, and unified Termux build pipeline.
- **Phase 9 (Next)**: Parametric CAD primitive kernel (cuboids, cylinders, spheres, cones) with dynamic CPU/GPU tessellation buffers.
- **Phase 10**: Vulkan overlay UI (ImGui docking / toolbars) for interactive CAD manipulation, transformation gizmos, and viewport camera controls.
- **Phase 11**: CAD file import/export pipeline (STEP / IGES / STL / OBJ) and scene persistence.
