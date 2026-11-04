#!/data/data/com.termux/files/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT_DIR}"

# 1. Environment & Tools
export ANDROID_JAR="${ANDROID_JAR:-/data/data/com.termux/files/home/android-sdk/platforms/android-34/android.jar}"
export ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-/data/data/com.termux/files/home/android-sdk/ndk/26.1.10909125}"
BUILD_TOOLS_DIR="${BUILD_TOOLS_DIR:-/data/data/com.termux/files/home/android-sdk/build-tools/34.0.0}"
D8="${BUILD_TOOLS_DIR}/d8"
TOOLCHAIN="${TOOLCHAIN:-$HOME/AI/arm64-ndk/arm64-android.toolchain.cmake}"

if [ ! -f "${ANDROID_JAR}" ]; then
    echo "ERROR: ANDROID_JAR not found at ${ANDROID_JAR}" >&2
    exit 1
fi
if [ ! -d "${ANDROID_NDK_HOME}" ]; then
    echo "ERROR: ANDROID_NDK_HOME not found at ${ANDROID_NDK_HOME}" >&2
    exit 1
fi

echo "=== MobileCAD Build Pipeline ==="
echo "Project Root: ${ROOT_DIR}"
echo "Android JAR:  ${ANDROID_JAR}"
echo "Android NDK:  ${ANDROID_NDK_HOME}"

# Ensure external/imgui exists
if [ ! -d "${ROOT_DIR}/external/imgui" ]; then
    echo "--- Cloning Dear ImGui into external/imgui ---"
    git clone --depth 1 -b docking https://github.com/ocornut/imgui.git "${ROOT_DIR}/external/imgui"
fi

# Ensure external/manifold exists
if [ ! -d "${ROOT_DIR}/external/manifold" ]; then
    echo "--- Cloning Manifold into external/manifold ---"
    git clone --depth 1 https://github.com/elalish/manifold.git "${ROOT_DIR}/external/manifold"
fi

# 2. Compile Shaders

echo "--- Compiling shaders ---"
"${SCRIPT_DIR}/compile_shaders.sh"


# 3. Native C++ Build (16 KB aligned)
echo "--- Building libmobilecad.so (Ninja) ---"
cmake -S ndk-build -B build \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
    -DCMAKE_BUILD_TYPE=Debug -G Ninja
ninja -C build

# 4. Generate 16 KB-aligned libc++_shared.so (cached)
LIBCXX_CACHED="build/mcad-tmp/libc++_shared.so"
if [ ! -f "${LIBCXX_CACHED}" ] || ! readelf -l "${LIBCXX_CACHED}" | grep -q "0x4000"; then
    echo "--- Re-linking 16 KB-aligned libc++_shared.so ---"
    mkdir -p build/mcad-tmp
    clang++ -shared --target=aarch64-linux-android34 \
        -nostdlib++ \
        -Wl,-z,max-page-size=16384 \
        -Wl,-soname,libc++_shared.so \
        -Wl,--whole-archive \
          "${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_static.a" \
          "${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++abi.a" \
        -Wl,--no-whole-archive \
        -rtlib=compiler-rt -unwindlib=libunwind \
        -resource-dir="${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17" \
        -L"${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17/lib/linux/aarch64" \
        -L"${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/34" \
        -lc -lm -ldl \
        -o "${LIBCXX_CACHED}"
fi

# 5. Build classes.dex from Java stubs
echo "--- Compiling classes.dex ---"
rm -rf build/mcad-tmp/java build/mcad-tmp/classes build/mcad-tmp/dex
mkdir -p build/mcad-tmp/java build/mcad-tmp/classes build/mcad-tmp/dex
echo 'package com.mobilecad; public class R {}' > build/mcad-tmp/java/R.java
echo 'package com.mobilecad; public class BuildConfig {}' > build/mcad-tmp/java/BuildConfig.java
javac -source 8 -target 8 -bootclasspath "${ANDROID_JAR}" -d build/mcad-tmp/classes build/mcad-tmp/java/*.java 2>/dev/null
bash "${D8}" --lib "${ANDROID_JAR}" --output build/mcad-tmp/dex build/mcad-tmp/classes/com/mobilecad/*.class

# 6. Assemble staging directory
echo "--- Assembling staging directory ---"
# Canonical source manifest: prefer ndk-build/AndroidManifest.xml, fallback to staging/AndroidManifest.xml
MANIFEST_SRC="${ROOT_DIR}/ndk-build/AndroidManifest.xml"
if [ ! -f "${MANIFEST_SRC}" ]; then
    MANIFEST_SRC="${ROOT_DIR}/staging/AndroidManifest.xml"
fi

rm -rf staging
mkdir -p staging/lib/arm64-v8a staging/assets
cp "${MANIFEST_SRC}" staging/AndroidManifest.xml
cp build/libmobilecad.so staging/lib/arm64-v8a/
cp "${LIBCXX_CACHED}" staging/lib/arm64-v8a/
cp -r assets/* staging/assets/
mkdir -p staging/assets/shaders
cp -f assets/shaders/*.spv staging/assets/shaders/
cp build/mcad-tmp/dex/classes.dex staging/

# 7. Package, Align, and Sign APK
echo "--- Packaging APK ---"
rm -f mobilecad-base.apk mobilecad-aligned.apk

aapt2 link -I "${ANDROID_JAR}" --manifest staging/AndroidManifest.xml -A staging/assets -o mobilecad-base.apk

# Add classes.dex and uncompressed .so files (-0 for mmap page alignment)
(cd staging && zip -ur ../mobilecad-base.apk classes.dex)
(cd staging && zip -0 -ur ../mobilecad-base.apk lib/arm64-v8a/)

echo "--- Aligning APK ---"
if zipalign -f -P 16 4 mobilecad-base.apk mobilecad-aligned.apk 2>/dev/null; then
    echo "  Aligned with -P 16 4 (16 KB page-size)"
else
    zipalign -f -p 4 mobilecad-base.apk mobilecad-aligned.apk
    echo "  Aligned with -p 4"
fi

# Ensure debug keystore exists
if [ ! -f "debug.keystore" ]; then
    echo "--- Creating debug.keystore ---"
    keytool -genkey -v -keystore debug.keystore -storepass android -alias androiddebugkey \
      -keypass android -keyalg RSA -keysize 2048 -validity 10000 \
      -dname "CN=Android Debug,O=Android,C=US"
fi

echo "--- Signing APK ---"
apksigner sign --ks debug.keystore --ks-pass pass:android --key-pass pass:android \
  --ks-key-alias androiddebugkey mobilecad-aligned.apk
apksigner verify mobilecad-aligned.apk

echo "=== Build Successful: $(pwd)/mobilecad-aligned.apk ==="
mkdir -p build/apk
cp mobilecad-aligned.apk build/apk/mobilecad-aligned.apk
cp mobilecad-aligned.apk build/apk/MobileCAD-v1.1.0-arm64-v8a.apk
cp mobilecad-aligned.apk build/apk/MobileCAD-arm64-v8a.apk

# 8. Optional Auto-Install if device connected
if command -v adb &>/dev/null && adb devices | grep -E "device$" >/dev/null 2>&1; then
    DEVICE=$(adb devices | grep -E "device$" | head -n1 | awk '{print $1}')
    echo "--- Connected device found (${DEVICE}): Installing APK ---"
    adb -s "${DEVICE}" install -r mobilecad-aligned.apk
    echo "--- Launching com.mobilecad ---"
    adb -s "${DEVICE}" shell "am force-stop com.mobilecad; am start -n com.mobilecad/android.app.NativeActivity"
fi
