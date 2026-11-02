# MobileCAD

[![Platform](https://img.shields.io/badge/Platform-Android%2014%2B%20(API%2034%2B)-green.svg)](https://developer.android.com)
[![Architecture](https://img.shields.io/badge/Architecture-ARM64%20(arm64--v8a)-blue.svg)]()
[![Vulkan](https://img.shields.io/badge/Graphics-Vulkan%201.4-red.svg)](https://www.vulkan.org)
[![C++](https://img.shields.io/badge/Language-C%2B%2B20-orange.svg)]()
[![Page%20Size](https://img.shields.io/badge/ELF%20Alignment-16%20KB%20Compliant-brightgreen.svg)]()

A high-performance, native Android 3D CAD application built from the ground up in C++20 and **Vulkan 1.4** (`VK_API_VERSION_1_4`). Designed specifically for touchscreens and active styluses (Samsung S-Pen), MobileCAD bypasses bloated intermediate abstractions (no X11, no Wayland, no GLFW, no Gradle) by interfacing directly with Android's `NativeActivity` and `ANativeWindow`.

The entire engineering toolchain is self-hosting: configured, compiled, packaged, aligned, signed, and deployed **100% on-device** inside native Termux on a Samsung Galaxy S23 Ultra (Snapdragon 8 Gen 2 / Adreno 740 GPU).

---

## Technical Architecture

```
                  +----------------------------------------------+
                  |           Android OS / SurfaceFlinger        |
                  +----------------------------------------------+
                                         |
                                         v
                  +----------------------------------------------+
                  |         NativeActivity (ANativeWindow)       |
                  +----------------------------------------------+
                                         |
                +------------------------+------------------------+
                |                                                 |
                v                                                 v
  +---------------------------+                     +---------------------------+
  |    Touch & S-Pen Input    |                     |   Vulkan 1.4 Render Loop  |
  |  (AMotionEvent / ALooper) |                     |   (2-Frame Sync Ring)     |
  +---------------------------+                     +---------------------------+
                |                                                 |
                v                                                 v
  +---------------------------+                     +---------------------------+
  |      Arcball Camera       |                     |  Hardware Depth Buffer    |
  | (Orbit / Pan / Pinch-Zoom)|                     |     (VK_FORMAT_D32_SFLOAT)|
  +---------------------------+                     +---------------------------+
                |                                                 |
                +------------------------+------------------------+
                                         |
                                         v
                  +----------------------------------------------+
                  |       Dynamic Push Constants (MVP Matrix)    |
                  +----------------------------------------------+
                                         |
                                         v
                  +----------------------------------------------+
                  |       Slang SPIR-V 1.3 Shader Pipeline       |
                  |     (Adreno-optimized, bufferless cube)      |
                  +----------------------------------------------+
```

### Core Graphics Engine
- **Vulkan 1.4 API Core**: Initialized with `VK_API_VERSION_1_4` targeting modern mobile Vulkan drivers.
- **Hardware Depth Buffer**: `VK_FORMAT_D32_SFLOAT` device-local attachment with optimal tiling and `VK_IMAGE_ASPECT_DEPTH_BIT`, enabling clean occlusion for complex 3D meshes.
- **Two-Frame Synchronization Ring**: Double-buffered in-flight frame synchronization (`kMaxFrames = 2`) using dedicated fences and acquire/render semaphores, eliminating GPU pipeline stalls while respecting mobile thermal and driver constraints.
- **Dynamic Viewport & Scissor**: Viewport and scissor states are bound dynamically (`VK_DYNAMIC_STATE_VIEWPORT`, `VK_DYNAMIC_STATE_SCISSOR`) to automatically accommodate Android multi-window, split-screen, and orientation changes without costly pipeline recreations.
- **Push Constant Transform Dispatch**: High-throughput 64-byte vertex push constants (`VK_SHADER_STAGE_VERTEX_BIT`) dispatch the combined Model-View-Projection (MVP) matrix directly into fast command buffers without descriptor set allocation overhead.

### Mobile-Optimized Shaders
- **Slang Shading Language**: Shaders are authored in modern Slang (`.slang`) and compiled to SPIR-V.
- **Adreno Compatibility Profile (`glsl_450+SPIRV_1_3`)**: Compiled targeting SPIR-V 1.3 to comply with Qualcomm Adreno mobile driver linker specifications, preventing non-input/output variables (such as PushConstant blocks) from invading the `OpEntryPoint` interface list.
- **Algorithmic Bufferless Geometry**: The reference 3D CAD cube is procedurally evaluated in `simple.vert.slang` using vertex IDs, eliminating stack-allocated constant composite memory pressure.

### Custom 3D Math & Arcball Camera
- **Vulkan NDC Clip Space**: Custom header-only math library (`Vec3<T>`, `Mat4<T>`) designed strictly for Vulkan right-handed clip space:
  - Inverted Y-axis ($+Y$ down: $M_{1,1} = -\frac{1}{\tan(\text{fovy}/2)}$).
  - Depth range normalized to $[0, 1]$ ($M_{2,2} = \frac{zFar}{zNear - zFar}$, $M_{2,3} = \frac{zFar \cdot zNear}{zNear - zFar}$).
  - Canonical row-major matrix memory layout matching shader `row_major float4x4`.
- **Spherical CAD Arcball Camera**:
  - **Single-Finger / S-Pen Orbit**: Incremental azimuth (yaw) and elevation (pitch) rotation, clamped to $[-1.48, 1.48]$ radians to prevent gimbal lock.
  - **Two-Finger Pinch-to-Zoom**: Continuous distance scaling relative to touch cluster distance.
  - **Two-Finger Midpoint Pan**: Translates the camera focus target along the camera's local right and up axes proportional to distance and viewport height.

---

## 16 KB Page-Size Compliance

Modern Android 15+ devices and 64-bit ARM architectures require binaries to support **16 KB (16384 bytes)** virtual memory page sizes:
1. **Application Shared Library (`libmobilecad.so`)**: Linked with `-Wl,-z,max-page-size=16384` to ensure ELF `LOAD` segments are aligned to 16 KB boundaries.
2. **C++ Standard Library (`libc++_shared.so`)**: Because NDK r26 ships unaligned 4 KB `libc++_shared.so` prebuilts, MobileCAD's build pipeline dynamically re-links `libc++_static.a` and `libc++abi.a` with `-Wl,-z,max-page-size=16384`, caching the aligned runtime in `build/mcad-tmp/`.
3. **APK Packaging Alignment**: Native `.so` libraries are stored uncompressed (`zip -0`) and aligned using `zipalign -P 16 4`, satisfying both the 16 KB memory-mapping requirement and standard 4-byte zip alignment.

---

## Repository Structure

```
MobileCAD/
├── assets/
│   └── shaders/                     # Compiled SPIR-V shaders packaged into APK
│       ├── simple.vert.spv
│       └── simple.frag.spv
├── ndk-build/
│   ├── AndroidManifest.xml          # Canonical APK manifest (hasCode="true", NativeActivity)
│   └── CMakeLists.txt               # Primary NDK CMake configuration
├── scripts/
│   ├── build_apk.sh                 # Full automated build, package, sign, & install pipeline
│   ├── compile_shaders.sh           # Slang to SPIR-V compilation script
│   └── setup_android.sh             # Native Termux environment dependency installer
├── src/
│   ├── main.cpp                     # NativeActivity entry point (android_main, input event loop)
│   ├── app/
│   │   ├── CADApp.h                 # Main application controller & touch dispatcher
│   │   └── CADApp.cpp               # Vulkan swapchain, render pass, and frame orchestrator
│   ├── rendering/
│   │   ├── Camera.h                 # Spherical Arcball camera (orbit, pan, zoom)
│   │   ├── VulkanContext.{h,cpp}    # VkInstance, VkDevice, Surface, Queues, Sync objects
│   │   ├── Swapchain.{h,cpp}        # VkSwapchainKHR, VkImageViews, Depth buffer (D32_SFLOAT)
│   │   ├── Pipeline.{h,cpp}         # SPIR-V asset loader and VkShaderModule factory
│   │   └── shaders/
│   │       ├── simple.vert.slang    # Procedural 3D reference cube & MVP transform
│   │       └── simple.frag.slang    # Fragment color output
│   └── utils/math/
│       ├── Vec3.h                   # 3D vector math (dot, cross, normalize)
│       └── Mat4.h                   # 4x4 matrix transforms (perspective, lookAt, rotations)
└── README.md
```

---

## On-Device Build & Deployment Workflow

### 1. Prerequisites (Inside Native Termux)
Run the setup script to install Clang, CMake, Ninja, OpenJDK, and Android build tools:
```bash
./scripts/setup_android.sh
```

Ensure your environment variables are configured (e.g., in `~/.bashrc`):
```bash
export ANDROID_JAR=$HOME/android-sdk/platforms/android-34/android.jar
export ANDROID_NDK_HOME=$HOME/android-sdk/ndk/26.1.10909125
```

### 2. Compile Shaders
If modifying `.slang` sources, compile them to SPIR-V (using `slangc`):
```bash
./scripts/compile_shaders.sh
```

### 3. One-Command Build & Run
To compile `libmobilecad.so`, generate `classes.dex`, assemble the staging package, align to 16 KB, sign with debug keystore, and install over wireless ADB:
```bash
./scripts/build_apk.sh
```

If an active ADB device is connected (e.g. `adb connect 192.168.0.x:<port>`), `build_apk.sh` automatically installs `mobilecad-aligned.apk` and launches `com.mobilecad/android.app.NativeActivity`.

### 4. Live Logcat Monitoring
```bash
adb logcat -v threadtime -s MobileCAD
```

Expected startup sequence:
```text
MobileCAD: android_main ENTERED
MobileCAD: CADApp::init: starting
MobileCAD: VulkanContext created
MobileCAD: Swapchain created: format=37, extent=1080x2316
MobileCAD: Render pass initialized
MobileCAD: Pipeline layout initialized
MobileCAD: Graphics pipeline initialized
MobileCAD: Framebuffers initialized
MobileCAD: CADApp::init: done
MobileCAD: onWindowResized: rebuild complete swap=1080x2316
```

---

## Development Roadmap

- [x] **Phase 0 — Toolchain Foundation**: On-device ARM64 Clang/CMake/Ninja build system in Termux.
- [x] **Phase 1 — Vulkan 1.4 Render Loop**: Stable `NativeActivity` event loop, 2-frame synchronization ring, device-lost recovery.
- [x] **Phase 2 — Non-Gradle Packaging**: Standalone `aapt2`, `d8` stub dex generator, `zipalign`, and `apksigner` automation.
- [x] **Phase 3 — 16 KB Page-Size Compliance**: Segment alignment enforcement for `libmobilecad.so` and `libc++_shared.so`.
- [x] **Phase 4 — Diagnostic Cleanliness**: Silenced per-frame log spam and verified lifecycle transitions.
- [x] **Phase 5 — Dynamic Surface & Color**: Enabled dynamic viewport/scissor and resolved composite alpha blending.
- [x] **Phase 6 — 3D Projection & Depth Buffer**: Hardware `VK_FORMAT_D32_SFLOAT` depth buffer, push constant MVP dispatch, and procedural reference cube.
- [x] **Phase 7 — Touch & S-Pen CAD Camera**: Spherical Arcball camera supporting single-pointer orbit, two-pointer pinch zoom, and pan.
- [x] **Phase 9 — Memory Allocator & CAD Grid**: Device-local vertex/index buffers (`MeshBuffer`), XYZ triad, lit solid mesh pipeline, and CAD reference grid.
- [x] **Phase 10 — Viewport UI Overlay**: Dear ImGui Vulkan/Android HUD overlay, camera view presets, and parametric solid primitives.
- [x] **Phase 11 — Geometric Modeling Kernel & STL Export**: Manifold C++20 CSG Boolean kernel (Subtract/Drill Hole, Union, Intersect, Hollow Pipe), interactive parametric dimension sliders, and watertight binary STL export.

---

## License

Licensed under the [MIT License](LICENSE).
