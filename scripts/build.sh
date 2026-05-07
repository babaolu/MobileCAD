#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"

echo "=== MobileCAD Build Script ==="
echo ""

# Clean build directory
if [ "$1" == "clean" ] || [ "$1" == "-clean" ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    echo "Build directory cleaned."
    exit 0
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure
echo "Configuring with CMake..."
if [ -n "$VULKAN_INCLUDE_DIR" ] && [ -n "$VULKAN_LIBRARY" ]; then
    echo "  Using custom Vulkan paths:"
    echo "    VULKAN_INCLUDE_DIR=$VULKAN_INCLUDE_DIR"
    echo "    VULKAN_LIBRARY=$VULKAN_LIBRARY"
    cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DVulkan_INCLUDE_DIR="$VULKAN_INCLUDE_DIR" \
        -DVulkan_LIBRARY="$VULKAN_LIBRARY"
else
    cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug
fi

# Build
echo ""
echo "Building..."
ninja

echo ""
echo "Build complete! Run with: $BUILD_DIR/MobileCAD"
echo ""
