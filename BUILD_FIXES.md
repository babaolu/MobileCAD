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
7. [Complete Build Instructions](#7-complete-build-instructions)
8. [Files Modified](#8-files-modified)
9. [Key CMake Configuration](#9-key-cmake-configuration)

---

## 1. Missing Vulkan Video Headers

**Problem**:  
The NDK r26's `vulkan_core.h` includes multiple `vk_video/vulkan_video_*.h` headers (H.264, H.265, AV1, VP9). However, the sysroot path (`.../sysroot/usr/include/vulkan`) does not contain the `vk_video` subdirectory. The NDK does ship these headers under `sources/third_party/vulkan/src/include/vk_video`, but that copy is incomplete — it's missing the AV1 and VP9 codec headers entirely.

**Error observed**:
```
fatal error: 'vk_video/vulkan_video_codec_av1std.h' file not found
fatal error: 'vk_video/vulkan_video_codec_vp9std.h' file not found
```

**Solution**:  
Copy the complete set of `vk_video` headers from the Vulkan headers repository that is already included as a dependency of Slang in the project:

```
external/slang/build/_deps/vulkan_headers-src/include/vk_video/
```

**Implementation** (`ndk-build/CMakeLists.txt` lines 25–42):
```cmake
# Prepare isolated vk_video headers (avoid duplicate Vulkan C++ binding)
# The NDK's vulkan_core.h includes vk_video headers. The NDK's copy is incomplete.
# Use the full set from the Vulkan headers fetched as a dependency of Slang.
set(VULKAN_VIDEO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../external/slang/build/_deps/vulkan_headers-src/include/vk_video")
set(VULKAN_VIDEO_DST "${CMAKE_BINARY_DIR}/include")
if(EXISTS "${VULKAN_VIDEO_SRC}")
    file(MAKE_DIRECTORY "${VULKAN_VIDEO_DST}")
    file(COPY "${VULKAN_VIDEO_SRC}" DESTINATION "${VULKAN_VIDEO_DST}")
    message(STATUS "Copied vk_video headers to ${VULKAN_VIDEO_DST}/vk_video")
else()
    message(FATAL_ERROR "vk_video headers not found at ${VULKAN_VIDEO_SRC}")
endif()
```

The headers are copied to `${CMAKE_BINARY_DIR}/include/vk_video` and added to the compiler's include path via `target_compile_options` (see section 5).

---

## 2. Duplicate Shader Compilation Rules

**Problem**:  
Two CMakeLists files defined identical `compile_shaders` custom targets:
- Root `CMakeLists.txt` (lines 36–88)
- `app/src/main/cpp/CMakeLists.txt` (lines 55–96)

When configuring CMake, this produced:
```
CMake Error at CMakeLists.txt:65 (add_custom_command):
  Attempt to add a custom rule to output .../simple.spv.rule which already has a custom rule.
```

**Solution**:  
Avoid invoking the root CMakeLists directly. Instead, use `ndk-build/CMakeLists.txt` as the sole entry point:
```bash
cmake -S ndk-build -B build -DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake -G Ninja
```
The `ndk-build/CMakeLists.txt` does not include the root CMakeLists, so the duplicate shader rules are never processed. Shader compilation is performed separately via `./scripts/compile_shaders.sh`.

---

## 3. Android Event Loop API Update

**Problem**:  
`src/main.cpp` used the deprecated `android_app->pendingCommand` field and `android_app_clear_pending_cmd()` function, which were removed in NDK r26. The current API requires using `android_app_read_cmd()` to retrieve pending commands.

**Error observed**:
```
error: no member named 'pendingCommand' in 'android_app'
error: use of undeclared identifier 'android_app_clear_pending_cmd'
```

**Solution (`src/main.cpp` lines 1–9, 80–114)**:

1. Include `<exception>` for error handling.
2. Replace the entire event loop with the modern pattern:
```cpp
while (!state->destroyRequested) {
    int ident;
    int events;
    android_poll_source* source = nullptr;
    int timeoutMillis = g_running ? 0 : -1;

    // Poll for events; source will point to the poll source if ident >= 0
    ident = ALooper_pollOnce(timeoutMillis, nullptr, &events, reinterpret_cast<void**>(&source));

    if (ident >= 0 && source) {
        // Process the event (handles both input events and app commands)
        source->process(state, source);
    }

    if (g_running && g_cadApp) {
        try {
            g_cadApp->drawFrame();
        } catch (const std::exception& e) {
            LOGE("Exception in drawFrame: %s", e.what());
            g_running = false;
        }
    }
}
```
3. Add try-catch around CADApp construction and cleanup to prevent crashes from uncaught exceptions during Vulkan initialization.

---

## 4. Android Runtime Library Linking

**Problem**:  
The linker (lld) was invoked with `--target=aarch64-linux-android34` using the host's Clang 20 from Ubuntu. The host driver attempts to link against `libgcc` by default, which is not present in the Android NDK sysroot.

**Error observed**:
```
ld.lld: error: unable to find library -lgcc
ld.lld: error: unable to find library -lgcc
```

**Solution (`ndk-build/CMakeLists.txt` lines 88–99)**:

1. Link against the Android-specific compiler runtime libraries:
   - `libclang_rt.builtins-aarch64-android.a` — compiler-rt builtins
   - `libunwind.a` — stack unwinding for exceptions
2. Add linker flags to switch from libgcc to compiler-rt/libunwind:
```cmake
target_link_libraries(mobilecad PRIVATE
    ${android-lib}
    ${log-lib}
    android_native_app_glue
    Vulkan::Vulkan
    "${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17/lib/linux/libclang_rt.builtins-aarch64-android.a"
    "${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17/lib/linux/aarch64/libunwind.a"
)

target_link_options(mobilecad PRIVATE
    "-rtlib=compiler-rt"
    "-unwindlib=libunwind"
    "-resource-dir=${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17"
    "-L${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17/lib/linux"
    "-L${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17/lib/linux/aarch64"
)
```

Key flags:
- `-rtlib=compiler-rt` — use Clang's compiler-rt runtime instead of libgcc
- `-unwindlib=libunwind` — use libunwind for C++ exceptions
- `-resource-dir` — point to the NDK clang's builtin headers and libraries (version 17, matching the NDK's toolchain)
- `-L` paths — ensure the linker can find the static runtime libraries

---

## 5. Include Paths for Vulkan C++ Bindings

**Problem**:  
The NDK's `vulkan.hpp` (C++ bindings) includes `vulkan_core.h`, which in turn includes `vk_video/` headers. The compiler must find both:
1. The NDK's Vulkan C++ headers (`vulkan.hpp`, `vulkan_funcs.hpp`, etc.) — present in sysroot `usr/include/vulkan`
2. The `vk_video` headers — provided via our copied location

If include order is wrong, the standalone Vulkan headers from `external/slang/external/vulkan/include` could be picked first and cause conflicts.

**Solution (`ndk-build/CMakeLists.txt` lines defeating 83–86)**:
```cmake
target_compile_options(mobilecad PRIVATE
    "-I${CMAKE_SYSROOT}/usr/include"          # NDK sysroot: Vulkan C++ bindings first
    "-I${VULKAN_VIDEO_DST}"                   # Our copied vk_video headers second
)
```
Explicit ordering ensures `#include <vulkan/vulkan.hpp>` resolves to the NDK's copy, and its internal `#include "vk_video/..."` resolves to our complete header set.

---

## 6. APK Packaging

**Problem**:  
The provided `package_apk.sh` script uses `aapt2 link` with `-M` (deprecated) and expects a manifest that references app resources (`@drawable/ic_launcher`, `@string/app_name`) which do not exist in the project. The script also assumes the `res/` directory structure that isn't present.

**Solution**: Manual multi-step packaging:

1. **Compile shaders** via the provided script:
   ```bash
   ./scripts/compile_shaders.sh
   # Outputs: assets/shaders/simple.vert.spv, simple.frag.spv
   ```

2. **Create staging directory** with minimal manifest (`staging/AndroidManifest.xml`):
   ```xml
   <?xml version="1.0" encoding="utf-8"?>
   <manifest xmlns:android="http://schemas.android.com/apk/res/android"
       package="com.mobilecad"
       android:versionCode="1"
       android:versionName="1.0">
       <uses-sdk android:minSdkVersion="34" android:targetSdkVersion="34" />
       <uses-feature android:name="android.hardware.vulkan" android:required="true" />
       <application
           android:label="MobileCAD"
           android:hasCode="false"
           android:theme="@android:style/Theme.NoTitleBar.Fullscreen">
           <activity
               android:name="android.app.NativeActivity"
               android:exported="true"
               android:configChanges="orientation|screenSize|keyboardHidden"
               android:screenOrientation="landscape">
               <meta-data
                   android:name="android.app.lib_name"
                   android:value="mobilecad" />
               <intent-filter>
                   <action android:name="android.intent.action.MAIN" />
                   <category android:name="android.intent.category.LAUNCHER" />
               </intent-filter>
           </activity>
       </application>
   </manifest
   ```
   This manifest uses hardcoded strings and no resource references, eliminating the need for a `res/` directory.

3. **Build base APK** with aapt2 (compiles manifest to binary):
   ```bash
   mkdir -p staging/lib/arm64-v8a
   cp build/libmobilecad.so staging/lib/arm64-v8a/
   cp -r assets staging/
   aapt2 link -I $ANDROID_JAR --manifest staging/AndroidManifest.xml -A staging/assets -o mobilecad-base.apk
   ```

4. **Add native library** if not already bundled (aapt2 `-A` only adds assets, not native libs):
   ```bash
   cd staging && zip -ur ../mobilecad-base.apk lib/arm64-v8a/libmobilecad.so && cd ..
   ```

5. **Align** the APK:
   ```bash
   zipalign -v 4 mobilecad-base.apk mobilecad-aligned.apk
   ```

6. **Sign** the APK (debug keystore):
   ```bash
   if [ ! -f debug.keystore ]; then
       keytool -genkeypair -v -keystore debug.keystore -storepass android \
           -alias androiddebugkey -keypass android \
           -dname "CN=Android Debug,O=Android,C=US" \
           -keyalg RSA -keysize 2048 -validity 10000
   fi
   apksigner sign --ks debug.keystore --ks-pass pass:android \
       --key-pass pass:android --ks-key-alias androiddebugkey \
       mobilecad-aligned.apk
   ```

7. **Verify**:
   ```bash
   apksigner verify mobilecad-aligned.apk
   ```

---

## 7. Complete Build Instructions

```bash
# 1. Compile shaders (host tool, runs on ARM64 in PRoot)
./scripts/compile_shaders.sh

# 2. Configure CMake using the ndk-build entry point
cmake -S ndk-build -B build \
  -DCMAKE_TOOLCHAIN_FILE=/root/AI/arm64-ndk/arm64-android.toolchain.cmake \
  -G Ninja

# 3. Build the shared library
ninja -C build

# 4. Package the APK manually (see section 6)
#    - Create staging/ with minimal AndroidManifest.xml
#    - Copy build/libmobilecad.so to staging/lib/arm64-v8a/
#    - Copy assets/ to staging/
#    - aapt2 link, zip native lib, zipalign, apksigner

# Output: mobilecad-aligned.apk (signed, ready to install)
adb install -r mobilecad-aligned.apk
```

---

## 8. Files Modified

| File | Changes |
|---|---|
| `ndk-build/CMakeLists.txt` | Complete rewrite: handles vk_video header copying, Vulkan detection, android_native_app_glue build, include paths, linker flags for Android runtime, and packaging preparation |
| `src/main.cpp` | Updated event loop for NDK r26 API; added `<exception>`; wrapped rendering in try-catch; replaced deprecated `pendingCommand` logic with `ALooper_pollOnce` + `source->process` |
| `android/app/CMakeLists.txt` | Added `${NDK_ROOT}/sources/third_party/vulkan/src/include` to include path (not used in final build but kept for reference) |
| `staging/AndroidManifest.xml` | Created minimal manifest without resource dependencies for manual APK assembly |

---

## 9. Key CMake Configuration

**Toolchain** (`~/AI/arm64-ndk/arm64-android.toolchain.cmake`):
- Compiler: `/usr/lib/llvm-20/bin/clang++` (native ARM64 clang from Ubuntu)
- Target triple: `aarch64-linux-android34`
- Sysroot: `/data/data/com.termux/files/home/android-sdk/ndk/26.1.10909125/toolchains/llvm/prebuilt/linux-x86_64/sysroot`
- `CMAKE_FIND_ROOT_PATH` restricted to sysroot to prevent host contamination
- `CMAKE_TRY_COMPILE_TARGET_TYPE` set to `STATIC_LIBRARY` (cannot run cross-compiled test executables on host)

**Linker flags** (added in `ndk-build/CMakeLists.txt`):
- `-rtlib=compiler-rt` — use Clang's runtime library
- `-unwindlib=libunwind` — use libunwind for exception handling
- `-resource-dir` — NDK clang's resource directory (version 17)
- `-L` — additional library search paths for builtins and libunwind

**Include order** (critical for Vulkan headers):
1. `-I${CMAKE_SYSROOT}/usr/include` — NDK sysroot (vulkan.hpp, vulkan_core.h)
2. `-I${VULKAN_VIDEO_DST}` — copied `vk_video/` subdirectory with complete codec headers

---

## 10. Notes for Rebuilding

- Always clean the build directory (`rm -rf build`) after modifying `ndk-build/CMakeLists.txt` or toolchain settings.
- Shaders are compiled independently by `scripts/compile_shaders.sh` and placed in `assets/shaders/`. The APK packaging step copies from there.
- The APK packaging script `package_apk.sh` is not used in the final workflow due to `aapt2` compatibility issues; manual steps in section 6 are recommended.
- The build depends on the Vulkan headers fetched as part of the `external/slang` submodule. Ensure submodules are initialized:
  ```bash
  git submodule update --init --recursive
  ```
- If Vulkan validation layers are installed on the device, they will be enabled automatically in debug builds (ENABLE_VALIDATION_LAYERS = true when not NDEBUG).

---

**Last updated**: 2026-05-13  
**Build status**: ✅ `libmobilecad.so` compiles, APK signs and verifies successfully.
