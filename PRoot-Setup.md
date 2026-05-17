# PRoot Setup Guide for MobileCAD

## Why PRoot?

PRoot creates a full Linux distribution (Ubuntu/Debian) inside Termux, providing:
- Working `/tmp` directory (no permission errors)
- glibc compatibility (vs Android's Bionic)
- Standard Linux tools and package management (`apt`)
- Seamless Vulkan and graphics toolchain

## One-Time Setup

### 1. Install PRoot in Termux

```bash
pkg update
pkg install proot-distro
```

### 2. Install Ubuntu Distribution

```bash
proot-distro install ubuntu
```

This downloads and sets up Ubuntu rootfs (~2GB). Takes 10-30 minutes depending on connection.

### 3. Login to Ubuntu

```bash
proot-distro login ubuntu
```

You're now inside a Ubuntu Linux environment! The prompt will change to something like `user@ubuntu:~$`.

### 4. Install Dependencies (Inside Ubuntu)

```bash
apt update
apt install -y \
    clang \
    cmake \
    ninja-build \
    libvulkan-dev \
    libglfw3-dev \
    git \
    python3 \
    vulkan-tools \
    mesa-vulkan-icd-freedreno
```

### 5. Build and Install Slang

Slang is not in apt repos, so build from source:

```bash
# Clone
git clone https://github.com/shader-slang/slang.git
cd slang

# Initialize submodules
git submodule update --init --recursive

# Configure and build
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja

# Install slangc
sudo cp bin/slangc /usr/local/bin/

# Verify
slangc --version
cd /path/to/MobileCAD
```

## Building MobileCAD (Inside Ubuntu PRoot)

```bash
# Navigate to your project (it's accessible at the same path)
# Example: if project is in /data/data/com.termux/files/home/AI/MobileCAD
cd /data/data/com.termux/files/home/AI/MobileCAD

# Create build directory
mkdir -p build && cd build

# Configure
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Build
ninja

# Run (with Termux-X11 running)
export DISPLAY=:0
./MobileCAD
```

## Daily Workflow

1. Start Termux app
2. Enter PRoot: `proot-distro login ubuntu`
3. Navigate to project: `cd /data/data/com.termux/files/home/AI/MobileCAD`
4. Build: `cd build && ninja` (or `cmake --build build`)
5. Run: `export DISPLAY=:0 && ./build/MobileCAD`

## Tips

- **Persistent home**: Your Ubuntu home directory is separate. Use `/data/data/com.termux/files/home/...` to access Termux files.
- **Speed**: PRoot is fast; no significant performance penalty for development.
- **Updates**: To update Ubuntu packages: `apt update && apt upgrade`
- **Exit PRoot**: Type `exit` or press Ctrl+D
- **Multiple distros**: You can install `debian` alongside `ubuntu` if desired: `proot-distro install debian`

## Troubleshooting

### "Cannot open display"
- Ensure Termux-X11 app is running
- Check DISPLAY: `echo $DISPLAY` (should be `:0` or `:1`)
- Test: `xeyes` or `xterm` (install with `apt install x11-apps`)

### Vulkan not found
- Verify driver installed: `ls /usr/lib/x86_64-linux-gnu/libvulkan.so` (or `aarch64-linux-gnu` on ARM)
- Check: `vulkaninfo | head -20`

### CMake errors about /tmp
- This should not happen in PRoot; if it does, your PRoot environment may have issues

## Why Not Standard Termux?

Standard Termux uses Android's Bionic libc, which is not fully compatible with glibc-based tools. Many prebuilt Vulkan utilities (RenderDoc, shader compilers, etc.) expect glibc and fail on Bionic. PRoot gives you a real Linux environment with glibc, solving these compatibility issues while still running on Android.

## Alternative: Desktop Development

You can also develop on a regular Linux desktop/macOS and cross-compile for Android. That approach is beyond this guide's scope but offers more power and easier debugging.
