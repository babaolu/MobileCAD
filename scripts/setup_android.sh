#!/data/data/com.termux/files/usr/bin/env bash
set -euo pipefail

echo "=== MobileCAD Native Termux Setup ==="
echo ""

# Verify Termux environment
if [ ! -d "/data/data/com.termux/files/home" ]; then
    echo "WARNING: This script is intended for Termux on Android ARM64." >&2
fi

echo "Step 1: Updating package repository..."
pkg update -y

echo ""
echo "Step 2: Installing native build tools & SDK utilities..."
echo "  - clang (C++20 native ARM64 compiler)"
echo "  - cmake (build system generator)"
echo "  - ninja (fast build executor)"
echo "  - openjdk-17 (javac & keytool for classes.dex and debug keystore)"
echo "  - aapt2, zipalign, apksigner (APK assembly and signing)"
echo "  - android-tools (adb wireless debugging)"
echo "  - git, python, zip, unzip"
echo ""

pkg install -y clang cmake ninja openjdk-17 aapt2 zipalign apksigner android-tools git python zip unzip

echo ""
echo "=========================================================="
echo "Setup complete! Prerequisites:"
echo "1. Android SDK platform-34 android.jar at:"
echo "   export ANDROID_JAR=\$HOME/android-sdk/platforms/android-34/android.jar"
echo ""
echo "2. Android NDK r26 sysroot at:"
echo "   export ANDROID_NDK_HOME=\$HOME/android-sdk/ndk/26.1.10909125"
echo ""
echo "3. Slang shader compiler (slangc) built via PRoot or from source."
echo ""
echo "To build, package, sign, and install MobileCAD in one command:"
echo "   ./scripts/build_apk.sh"
echo "=========================================================="
