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

### Files Modified

| File | Key Changes |
|------|------------|
| `ndk-build/CMakeLists.txt` | vk_video header copy, Vulkan find, android_native_app_glue, link opts for compiler-rt/libunwind, resource-dir, -L paths |
| `src/main.cpp` | extern "C" on android_main, ALooper_pollOnce event loop, file logging to `/data/local/tmp/mobilecad.log`, `wait_retry` pattern waiting for window, inline lambda callbacks, try/catch |
| `android/app/CMakeLists.txt` | Added NDK third_party Vulkan include (unused in final build) |
| `staging/AndroidManifest.xml` | Created minimal manifest: `package=com.mobilecad`, hardcoded label, `android:hasCode="false"`, `android:extractNativeLibs="true"`, `android:debuggable="true"` |
| `BUILD_FIXES.md` | Comprehensive documentation of all fixes and build instructions |

---

## Current State

### Build succeeds ✅
```bash
./scripts/compile_shaders.sh
cmake -S ndk-build -B build -DCMAKE_TOOLCHAIN_FILE=~/AI/arm64-ndk/arm64-android.toolchain.cmake -G Ninja
ninja -C build        # → build/libmobilecad.so (509KB)
```

### APK built and signed ✅
- **APK:** `mobilecad-aligned.apk` (~605 KB, signed with debug keystore)
- **Location:** `~/storage/shared/Download/mobilecad-aligned.apk`
- **Contains:**
  - `AndroidManifest.xml` (binary, compiled by aapt2)
  - `assets/shaders/simple.vert.spv`, `simple.frag.spv`
  - `lib/arm64-v8a/libmobilecad.so`
  - `lib/arm64-v8a/libc++_shared.so`
- **Manifest:** `package=com.mobilecad`, `android.app.lib_name=mobilecad`, Vulkan required, `minSdkVersion=34`, debuggable

### Problem: App immediately closes on launch ❓

**Symptoms:**
- No `MobileCAD` logcat tag appears at all
- No `mobilecad.log` file created anywhere on the filesystem
- `android_main` confirmed NOT being entered (no log output)
- App closes instantly after tapping the icon

**Tested log file paths (all writable without permissions):**
- `/data/local/tmp/mobilecad.log`
- `/sdcard/mobilecad.log`
- `/sdcard/Download/mobilecad.log`
- `/sdcard/Android/data/com.mobilecad/files/mobilecad.log`

### Hypotheses for Why `android_main` Is Never Called

1. **Library loads but `android_main` symbol name is wrong** — checked: `readelf` shows `android_main` as `GLOBAL DEFAULT`, correct name. The stub in `libandroid_native_app_glue.a` should find it.

2. **`libmobilecad.so` fails `dlopen` before reaching `android_main`** — e.g., missing transitive dependency, or a `.init_array` / `__attribute__((constructor))` function crashes before `android_main`. Current code has **no** global objects or constructors beyond a static `AutoInitialized` (removed in latest build). The `g_logFile` static FILE* is zero-initialized.

3. **ClassLoader/zygote process kills the native process before our code runs** — can happen if the library has a bad ELF header or unmet symbol dependency.

4. **NativeActivity cannot find the library at the expected path** — the `android:hasCode="false"` + `android.app.lib_name=mobilecad` combo requires the `.so` to be at `lib/arm64-v8a/libmobilecad.so` inside the APK, which it is. But if `android:extractNativeLibs` is ignored or the extract path is wrong, load fails.

5. **Insufficient native heap or linker bug** on this specific Android 14 build.

### What to Try Next (in order)

**A. Confirm the library is actually being loaded:**
The most important-remaining diagnostic is to check whether `libmobilecad.so` is loaded at all when
the app launches, and if `dlopen` returns an error. This must be verified directly via
`logcat` or by checking the native library directory under the app's data dir. I cannot
access the device yet so I cannot verify directly from here, but the next step is:
Check `/data/data/com.mobilecad/files/` and `/data/data/com.mobilecad/lib/` after
installing and attempting to launch.

**B. Read logcat via ADB from Termux:**
The crash log needs to be observed live on device. In Termux setup the previous session I could not yet
connect with adb. Repeating the instruction from the conversation that must be performed on the
device itself in Termux out of PRoot:

Termux outside of proot-distro:
```
adb connect localhost:5555
adb logcat *:* MobileCAD:* -v threadtime
```

Then open the app. If nothing useful appears in logcat at all, the process is being killed in Java
before any native code runs. If a Kernel/tombstone/signal is referenced, find the PID via logcat
and inspect `/data/tombstones/tombstone_XX` via `adb pull` or by opening the file directly on
device. Even with `android:debuggable=true`, process death due to a `SIGSEGV` in a constructor
before `android_main` will never reach file-logging calls.

**C. Verify APK contents:**
Confirm the just-installed APK has both `.so` files intact at those exact paths with `adb shell`:
```
run-as com.mobilecad ls -la /data/app/~/base.apk!lib/arm64-v8a/
```
or extract with `unzip -l`.

**D. Add `-Wl,--no-undefined` linker flag** to ensure no undefined symbols slip through.

**E. Try `-fsyntax-only` to confirm no constructor elision issues** before linking.

**F. Check for `android:extractNativeLibs` having any effect** on this Android 14 build by checking
`/data/app/~~base.apk!/lib/arm64-v8a/` after install.

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
rm -rf staging && mkdir -p staging/lib/arm64-v8a
cp -r /data/data/com.termux/files/home/android-sdk/ndk/26.1.10909125/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so staging/lib/arm64-v8a/
cp build/libmobilecad.so staging/lib/arm64-v8a/
cp -r assets staging/
cat > staging/AndroidManifest.xml <<'EOF'
[manifest contents — see above]
EOF

aapt2 link -I /data/data/com.termux/files/home/android-sdk/platforms/android-34/android.jar \
  --manifest staging/AndroidManifest.xml -A staging/assets -o mobilecad-base.apk
cd staging && zip -ur ../mobilecad-base.apk lib/arm64-v8a/ && cd ..
zipalign -v 4 mobilecad-base.apk mobilecad-aligned.apk
apksigner sign --ks debug.keystore --ks-pass pass:android --key-pass pass:android \
  --ks-key-alias androiddebugkey mobilecad-aligned.apk
apksigner verify mobilecad-aligned.apk

# Copy to shared storage for file-manager install
cp mobilecad-aligned.apk ~/storage/shared/Download/
```

---

## Key Paths

| Path | Purpose |
|------|---------|
| `ndk-build/CMakeLists.txt` | Primary CMake entry (vk_video copy, find_package, link opts) |
| `src/main.cpp` | NDK entry point, event loop, logging |
| `src/app/CADApp.cpp` / `.h` | CADApp (Vulkan init, drawFrame, cleanup) |
| `src/rendering/VulkanContext.cpp` / `.h` | VulkanContext (instance, device, surface, queues) |
| `src/rendering/Swapchain.cpp` / `.h` | Swapchain creation and management |
| `src/rendering/Pipeline.cpp` / `.h` | Shader loading (AAssetManager) and module creation |
| `external/slang/build/_deps/vulkan_headers-src/` | Complete vk_video headers (source of truth) |
| `assets/shaders/` | SPIR-V shaders (`simple.vert.spv`, `simple.frag.spv`) |
| `BUILD_FIXES.md` | Detailed documentation of all build fixes |

---

## Known Unknowns / Open Questions

- Why is `android_main` never entered? No log file at all, no logcat output under `MobileCAD` tag.
- Is `libmobilecad.so` even loading? Need to verify via `logcat` or `run-as`/`pm path`.
- Any tombstone in `/data/tombstones/` after crash?
- Is `android:extractNativeLibs="true"` effective on this Android 14 build?

## What the Next Session Should Do

1. Re-examine this file (`MOBILECAD_CONTEXT.md`) to understand full history.
2. Try running `adb logcat` live while launching the app (from Termux outside proot, using `adb connect localhost:5555`).
3. If logcat shows nothing for MobileCAD, inspect `logcat -b crash` and `/data/tombstones/`.
4. If a tombstone exists, share the tombstone file contents.
5. Consider removing `android:hasCode="false"` if that affects the NativeActivity classloader behavior.
6. Consider verifying NDK sysroot's `libc.so`, `libm.so`, `libdl.so` are available in APK environment.
7. Consider adding a trivial `JNI_OnLoad` that logs via `__android_log_print` immediately on load as an additional test.
8. If still stuck, try `linkerconfig` or verify APK's `classes.dex` (NativeActivity class) isn't the issue.
