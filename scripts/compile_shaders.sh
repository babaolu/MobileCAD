#!/data/data/com.termux/files/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SHADER_SRC_DIR="${PROJECT_DIR}/src/rendering/shaders"
OUTPUT_DIR="${PROJECT_DIR}/shaders/compiled"
ASSETS_DIR="${PROJECT_DIR}/assets/shaders"

mkdir -p "$OUTPUT_DIR"
mkdir -p "$ASSETS_DIR"

echo "Checking for Slang compiler (slangc)..."

USE_PROOT=0
if command -v slangc &> /dev/null; then
    echo "  Found native slangc at: $(which slangc)"
elif command -v proot-distro &> /dev/null && proot-distro login ubuntu -- which slangc &> /dev/null; then
    USE_PROOT=1
    echo "  Found slangc inside PRoot Ubuntu"
else
    echo "ERROR: slangc not found in PATH or in PRoot Ubuntu."
    echo ""
    echo "Slang shader compiler is required to build shaders."
    echo "Install slangc or set up Ubuntu via proot-distro."
    exit 1
fi

echo ""
echo "Compiling Slang shaders to SPIR-V 1.3..."
for slang in "$SHADER_SRC_DIR"/*.slang; do
    if [ -f "$slang" ]; then
        filename=$(basename "$slang")
        base="${filename%.slang}"
        # Determine shader stage from filename prefix (e.g., simple.vert.slang -> vertex)
        if [[ "$filename" == *.vert.slang ]]; then
            stage="vertex"
        elif [[ "$filename" == *.frag.slang ]]; then
            stage="fragment"
        elif [[ "$filename" == *.comp.slang ]]; then
            stage="compute"
        else
            echo "  WARNING: Unknown shader stage for $filename, skipping"
            continue
        fi
        echo "  Compiling $filename (stage: $stage)..."
        out_spv="$OUTPUT_DIR/$base.spv"
        if [ $USE_PROOT -eq 1 ]; then
            proot-distro login ubuntu -- slangc "$slang" -target spirv -profile glsl_450+SPIRV_1_3 -entry main -stage "$stage" -o "$out_spv"
        else
            slangc "$slang" -target spirv -profile glsl_450+SPIRV_1_3 -entry main -stage "$stage" -o "$out_spv"
        fi
        cp "$out_spv" "$ASSETS_DIR/$base.spv"
        echo "    -> $out_spv (and $ASSETS_DIR/$base.spv)"
    fi
done

echo ""
echo "Shader compilation complete. Assets synced."
echo ""

