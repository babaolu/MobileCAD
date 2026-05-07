#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SLANG_DIR="${PROJECT_DIR}/external/slang"

echo "=== Build Slang Shader Compiler from Source ==="
echo ""

# Check if we're in Termux
if ! [ -d "/data/data/com.termux/files/home" ]; then
    echo "Warning: This script is intended for Termux on Android."
    read -p "Continue anyway? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Ensure dependencies
echo "Checking dependencies..."
if ! command -v cmake &> /dev/null; then
    echo "ERROR: cmake not found. Run ./scripts/setup_android.sh first."
    exit 1
fi

if ! command -v ninja &> /dev/null; then
    echo "ERROR: ninja not found. Run ./scripts/setup_android.sh first."
    exit 1
fi

if ! command -v python &> /dev/null; then
    echo "ERROR: python not found. Run ./scripts/setup_android.sh first."
    exit 1
fi

# Clone Slang if not present
if [ ! -d "${SLANG_DIR}" ]; then
    echo "Cloning Slang repository into external/slang ..."
    mkdir -p external
    git clone https://github.com/shader-slang/slang.git "${SLANG_DIR}"
fi

# Build Slang
cd "${SLANG_DIR}"
echo "Updating Slang submodules..."
git submodule update --init --recursive

BUILD_DIR="${SLANG_DIR}/build"
if [ "$1" == "clean" ] || [ "$1" == "-clean" ]; then
    echo "Cleaning previous Slang build..."
    rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo ""
echo "Configuring Slang build (Release)..."
echo "  This will take several minutes on a phone."
echo "  You can run this in a screen/tmux session to avoid interruptions."
echo ""
read -p "Press Enter to continue or Ctrl+C to cancel..."

cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release

echo ""
echo "Building Slang (this is the slow part - 10-30 minutes on mobile)..."
ninja

echo ""
echo "Installing slangc to /usr/local/bin ..."
cp bin/slangc /data/data/com.termux/files/usr/bin/
chmod +x /data/data/com.termux/files/usr/bin/slangc

echo ""
echo "✓ Slang compiler installed successfully!"
echo "  slangc is now available at: $(which slangc 2>/dev/null || echo /data/data/com.termux/files/usr/bin/slangc)"
echo ""
echo "You can now build the MobileCAD project:"
echo "  cd ${PROJECT_DIR}"
echo "  ./scripts/build.sh"
echo ""
