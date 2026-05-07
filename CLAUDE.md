# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MobileCAD is a native Android CAD application built with C++20 and Vulkan using Android NativeActivity. No X11, no GLFW — the app runs directly on Android via `ANativeWindow` and the NDK's `android_native_app_glue` library. Shaders are written in Slang and compiled to SPIR-V at build time on the host.

**Current State**: The project has a foundational Vulkan rendering pipeline working with a simple colored triangle demo. Core infrastructure includes Vulkan context management with NDK surface creation, swapchain handling, and graphics pipeline setup. The codebase follows modern C++ practices with RAII and C++ Core Guidelines.

## Tech Stack

- **Graphics**: Vulkan 1.2+ (via vulkan.hpp C++ bindings)
- **Windowing/Input**: Android NDK + NativeActivity + ANativeWindow
- **Shader Language**: Slang (.slang → .spv, compiled on host)
- **Build System**: CMake 3.20+ for native code, Gradle 8+ for APK packaging
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

- **Android NDK**: r26+ (includes Clang, Vulkan headers, android_native_app_glue)
- **Android SDK**: Command-line tools, build-tools, platform-sdk-34
- **Build**: Gradle 8+, CMake 3.20+, Ninja
- **Host**: Any Linux/macOS system to compile shaders with `slangc`
- **Shaders**: Slang compiler (`slangc`) or `glslangValidator` (compile on host, not device)

### Quick Start

```bash
# Build APK
cd android
./gradlew assembleDebug

# Install on device
adb install -r app/build/outputs/apk/debug/app-debug.apk

# Launch (or tap the app icon)
adb shell am start -n com.mobilecad.app/android.app.NativeActivity

# View logs
adb logcat MobileCAD:* *:S
```

### Shader Compilation

Shaders are compiled on the **host** (your build machine) at CMake build time, then bundled into the APK as assets:

```bash
# Manual shader compilation (host)
./scripts/compile_shaders.sh

# Check compiled shaders exist in the APK
ls android/app/src/main/assets/shaders/*.spv
```

Shaders are auto-copied to `android/app/src/main/assets/shaders/` during the Gradle build.

### Linting & Formatting

```bash
# Format all C++ files
clang-format -i src/**/*.cpp src/**/*.h

# Check formatting (dry-run)
clang-format --dry-run --Werror src/**/*.cpp src/**/*.h
```

**Note**: `.clang-format` uses Google style with Allman braces, 4-space indent, left pointer alignment.

### Debugging

- **Debug build**: Gradle debug variant includes:
  - NDK debug symbols
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
# Build and install
cd android && ./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk

# Launch
adb shell am start -n com.mobilecad.app/android.app.NativeActivity

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
| Black screen | Shaders not bundled | Run `./gradlew assembleDebug` again, check `assets/shaders/` |
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
# Build debug APK
cd android && ./gradlew assembleDebug

# Build release APK
cd android && ./gradlew assembleRelease

# Clean build
cd android && ./gradlew clean

# Install APK
adb install -r app/build/outputs/apk/debug/app-debug.apk

# View logs
adb logcat MobileCAD:* *:S

# Uninstall
adb uninstall com.mobilecad.app
```

### CMake Details

Root `CMakeLists.txt`:
- Handles shader compilation (runs at configure time on host)
- Finds Vulkan on the build machine

`android/app/CMakeLists.txt`:
- Configures NDK toolchain
- Builds `mobilecad.so` shared library
- Links: `android`, `log`, `android_native_app_glue`, `Vulkan::Vulkan`
- Copies shaders to APK assets

Build types via Gradle:
- **debug**: NDK debug symbols, sanitizers possible
- **release**: full optimizations, no debug overhead

Shader compilation: Integrated via `add_custom_command`/`add_custom_target`. Compiles `.slang` → `.spv` on host. Shaders copied to APK assets directory by Gradle post-build step.

### Dependencies & Version Requirements

- **Android NDK**: r26+
- **Android SDK**: minSdk 34 (Android 14), targetSdk 34
- **Vulkan**: 1.2+ on device
- **Gradle**: 8+
- **CMake**: 3.20+
- **Ninja**: any recent version
- **Slang**: latest from source (compiled on host for APK)
