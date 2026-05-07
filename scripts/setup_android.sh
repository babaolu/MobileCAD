#!/bin/bash
set -e

echo "=== MobileCAD Android/Termux Setup ==="
echo ""

# Check if we're in Termux
if ! [ -d "/data/data/com.termux/files/home" ]; then
    echo "Warning: This script is intended for Termux on Android."
    echo "Some commands may not work on other platforms."
    read -p "Continue anyway? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

echo "Step 1: Enabling x11-repo (required for graphics libraries)..."
pkg install -y x11-repo 2>/dev/null || echo "x11-repo may already be enabled"

echo ""
echo "Step 2: Updating package list..."
pkg update -y

echo ""
echo "Step 3: Installing core build dependencies..."
echo "  - clang (C++20 compiler)"
echo "  - cmake (build system)"
echo "  - ninja (fast build tool)"
echo "  - glfw (windowing/input with Vulkan support)"
echo "  - vulkan-headers (Vulkan API headers)"
echo "  - mesa-vulkan-icd-freedreno (Vulkan driver for Adreno GPUs)"
echo "  - git (for optional Slang build)"
echo "  - python (required by Slang build)"
echo "  - vulkan-tools (optional: glslangValidator if not using Slang)"
echo ""
pkg install -y clang cmake ninja glfw vulkan-headers mesa-vulkan-icd-freedreno git python vulkan-tools

echo ""
echo "Step 4: Checking for Slang compiler (slangc)..."

if command -v slangc &> /dev/null; then
    echo "  ✓ slangc found: $(which slangc)"
else
    echo "  ✗ slangc NOT found"
    echo ""
    echo "  IMPORTANT: The Slang shader compiler is NOT available in Termux repos."
    echo "  You have three options to obtain it:"
    echo ""
    echo "  Option A - Build from source (RECOMMENDED, clean):"
    echo "    Run: ./scripts/build_slang.sh"
    echo "    This will compile Slang from source (~10-30 min on phone)."
    echo ""
    echo "  Option B - Use prebuilt binary with termux-chroot:"
    echo "    1. Install: pkg install proot"
    echo "    2. Download linux-aarch64 slangc from Slang GitHub releases"
    echo "    3. Run: termux-chroot /path/to/slangc"
    echo ""
    echo "  Option C - Switch to GLSL (not recommended if you want Slang features):"
    echo "    Rewrite shaders in GLSL and use glslangValidator from vulkan-tools"
    echo ""
    echo "  For now, you can still build the C++ code, but shaders won't compile."
fi

echo ""
echo "==========================================="
echo "Core setup complete! Installed:"
echo "  ✓ Clang, CMake, Ninja"
echo "  ✓ GLFW (with Vulkan support)"
echo "  ✓ Vulkan headers"
echo "  ✓ Mesa Vulkan ICD (freedreno)"
echo "  ✓ vulkan-tools (glslangValidator)"
echo ""
echo "To build the project:"
echo "  mkdir -p build && cd build"
echo "  cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug"
echo "  ninja"
echo ""
echo "Or use the build script:"
echo "  ./scripts/build.sh"
echo ""
echo "If you need Slang compiler for .slang shaders, run:"
echo "  ./scripts/build_slang.sh"
echo ""
echo "Runtime requirements:"
echo "  - Vulkan-capable GPU (Android 7+)"
echo "  - X11/Wayland display server (Termux-X11 from F-Droid)"
echo "  - Set DISPLAY=:0"
echo ""
