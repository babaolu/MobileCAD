#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SHADER_SRC_DIR="${PROJECT_DIR}/src/rendering/shaders"
OUTPUT_DIR="${PROJECT_DIR}/shaders/compiled"

mkdir -p "$OUTPUT_DIR"

echo "Checking for Slang compiler (slangc)..."

if ! command -v slangc &> /dev/null; then
    echo "ERROR: slangc not found in PATH."
    echo ""
    echo "Slang shader compiler is required to build shaders."
    echo ""
    echo "On Termux, you must build Slang from source:"
    echo "  cd ${PROJECT_DIR}"
    echo "  ./scripts/build_slang.sh"
    echo ""
    echo "Alternatively, on desktop Linux/macOS:"
    echo "  - Install: brew install slang (macOS) or build from source on Linux"
    echo "  - Or download prebuilt binary from https://github.com/shader-slang/slang/releases"
    echo ""
    exit 1
fi

echo "  Found slangc at: $(which slangc)"
echo ""
echo "Compiling Slang shaders to SPIR-V..."
for slang in "$SHADER_SRC_DIR"/*.slang; do
    if [ -f "$slang" ]; then
        base=$(basename "$slang" .slang)
        echo "  Compiling $slang..."
        slangc "$slang" -target spirv -entry main -o "$OUTPUT_DIR/$base.spv"
        echo "    -> $OUTPUT_DIR/$base.spv"
    fi
done

echo ""
echo "Shader compilation complete. Output in: $OUTPUT_DIR"
echo ""

