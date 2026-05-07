# MobileCAD

A native Android CAD application built with C++20 and Vulkan, using Android NDK NativeActivity. No X11, no GLFW — runs directly on Android with zero translation overhead.

## Features

- C++20 with strict C++ Core Guidelines compliance
- Vulkan 1.2+ for high-performance graphics
- Android NDK NativeActivity (direct ANativeWindow)
- Slang shader language with host compilation
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

### Build APK

```bash
cd android
./gradlew assembleDebug
```

### Install on Device

```bash
# Install via ADB
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

### Launch

```bash
# Via ADB
adb shell am start -n com.mobilecad.app/android.app.NativeActivity

# Or tap the app icon on your device
```

### View Logs

```bash
adb logcat MobileCAD:* *:S
```

### Build Options

- `./gradlew assembleDebug` - Debug build with NDK symbols
- `./gradlew assembleRelease` - Release build (no debug overhead)
- `./gradlew clean` - Clean build directory

### Manual Shader Compilation

```bash
./scripts/compile_shaders.sh
```

Shaders compile to `shaders/compiled/*.spv` and are copied to `android/app/src/main/assets/shaders/` during the Gradle build.

## Project Structure

```
MobileCAD/
├── CMakeLists.txt              # Root build config (shader compilation on host)
├── src/
│   ├── main.cpp                # NDK entry point (android_main + NativeActivity glue)
│   ├── app/                    # Application logic
│   │   └── CADApp.cpp/h        # Main app class (orchestrates rendering)
│   ├── rendering/              # Vulkan rendering
│   │   ├── VulkanContext.cpp/h # Vulkan: instance, device, Android surface, sync
│   │   ├── Swapchain.cpp/h     # Swapchain management
│   │   ├── Pipeline.cpp/h      # Shader module loading
│   │   └── shaders/            # Slang shader sources
│   └── utils/                  # Utilities
│       ├── math/Mat4.h, Vec3.h # Custom math types
│       └── Result.h            # Error handling
├── android/                    # Android NDK project
│   ├── app/
│   │   ├── build.gradle.kts    # App Gradle config
│   │   ├── CMakeLists.txt      # NDK CMake config
│   │   └── src/main/
│   │       └── AndroidManifest.xml  # NativeActivity, Vulkan feature flags
│   └── *.gradle.kts            # Gradle build files
├── shaders/compiled/           # Generated SPIR-V files (gitignored)
├── scripts/
│   ├── compile_shaders.sh      # Host shader compilation
│   └── build_slang.sh          # Build Slang from source
└── README.md                   # This file
```

## Development

### Code Style

- Follow C++ Core Guidelines
- Uses clang-format (Google style, Allman braces, 4-space indent)
- Run `clang-format -i src/*.cpp src/*.h` to format

### Debugging

- Vulkan validation layers enabled in debug builds (if installed on device)
- App logs to logcat with `MobileCAD` tag
- Use `adb logcat MobileCAD:* *:E` to filter for errors
- RenderDoc can capture frames on device via ADB

### Adding New Shaders

1. Create `.slang` file in `src/rendering/shaders/`
2. Gradle will compile it to SPI-R-V and bundle in the APK
3. Load with `Pipeline::loadShader("shaders/your_shader.spv")`

### Architecture

See [CLAUDE.md](CLAUDE.md) for detailed architecture and development guidelines.

## Troubleshooting

### App crashes on launch
- Check logcat: `adb logcat MobileCAD:* *:E`
- Ensure device supports Vulkan 1.2+
- Verify shaders are bundled in APK

### Black screen / triangle not visible
- Ensure shaders compiled: check `android/app/src/main/assets/shaders/`
- Rebuild with `./gradlew clean assembleDebug`

### "Failed to create Android surface"
- ANativeWindow was null or invalid
- Check that `APP_CMD_INIT_WINDOW` was received before Vulkan initialization

### Build fails with "NDK not found"
- Set `ANDROID_NDK_HOME` environment variable
- Or create `android/local.properties` with `ndk.dir=/path/to/ndk`

## License

TBD
