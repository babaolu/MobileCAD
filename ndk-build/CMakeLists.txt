cmake_minimum_required(VERSION 3.20)
project(mobilecad)

message(STATUS "CMAKE_SYSROOT=${CMAKE_SYSROOT}")
message(STATUS "NDK_ROOT=${NDK_ROOT}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# ----------------------------------------------------------------------
# NDK paths from toolchain
# ----------------------------------------------------------------------
if(NOT DEFINED NDK_ROOT)
    message(FATAL_ERROR "NDK_ROOT not set by toolchain file")
endif()

# API-level library directory (matches target triple aarch64-linux-android34)
set(NDK_LIB_DIR "${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/34")
message(STATUS "NDK_LIB_DIR=${NDK_LIB_DIR}")

# ----------------------------------------------------------------------
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

# ----------------------------------------------------------------------
# Find system libraries (android, log, vulkan) from NDK
# ----------------------------------------------------------------------
find_library(android-lib android PATHS "${NDK_LIB_DIR}" NO_DEFAULT_PATH)
find_library(log-lib     log    PATHS "${NDK_LIB_DIR}" NO_DEFAULT_PATH)

# Vulkan: provide hint for library location, then find package (for imported target)
set(Vulkan_LIBRARY "${NDK_LIB_DIR}/libvulkan.so" CACHE FILEPATH "Path to Vulkan library")
message(STATUS "Vulkan_LIBRARY hint set to: ${Vulkan_LIBRARY}")
if(NOT EXISTS "${Vulkan_LIBRARY}")
    message(WARNING "Vulkan library NOT found at: ${Vulkan_LIBRARY}")
endif()
find_package(Vulkan REQUIRED)
message(STATUS "Vulkan found: ${Vulkan_FOUND}, library: ${Vulkan_LIBRARY}")

# ----------------------------------------------------------------------
# Build android_native_app_glue from NDK source
# ----------------------------------------------------------------------
set(ANDROID_NATIVE_APP_GLUE_SRC "${NDK_ROOT}/sources/android/native_app_glue/android_native_app_glue.c")
add_library(android_native_app_glue STATIC ${ANDROID_NATIVE_APP_GLUE_SRC})
target_include_directories(android_native_app_glue PUBLIC "${NDK_ROOT}/sources/android/native_app_glue")

# ----------------------------------------------------------------------
# Shared library (loaded by NativeActivity)
# ----------------------------------------------------------------------
set(SOURCES
    ../src/main.cpp
    ../src/app/CADApp.cpp
    ../src/rendering/VulkanContext.cpp
    ../src/rendering/Swapchain.cpp
    ../src/rendering/Pipeline.cpp
    ../src/rendering/MeshBuffer.cpp
    ../external/imgui/imgui.cpp
    ../external/imgui/imgui_draw.cpp
    ../external/imgui/imgui_tables.cpp
    ../external/imgui/imgui_widgets.cpp
    ../external/imgui/backends/imgui_impl_android.cpp
    ../external/imgui/backends/imgui_impl_vulkan.cpp
)

add_library(mobilecad SHARED ${SOURCES})

# Include directories: source tree first
target_include_directories(mobilecad PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/imgui/backends
)


# Then add Vulkan include paths with explicit ordering:
# 1) NDK sysroot's C++ bindings (must come first)
# 2) isolated vk_video headers (afterwards)
target_compile_options(mobilecad PRIVATE
    "-I${CMAKE_SYSROOT}/usr/include"
    "-I${VULKAN_VIDEO_DST}"
)

target_link_libraries(mobilecad PRIVATE
    ${android-lib}
    ${log-lib}
    "-Wl,--whole-archive" android_native_app_glue "-Wl,--no-whole-archive"
    Vulkan::Vulkan
)

# Use compiler-rt and libunwind instead of libgcc, and point to NDK's clang resource libs
target_link_options(mobilecad PRIVATE
    "-Wl,-z,max-page-size=16384"
    "-rtlib=compiler-rt"
    "-unwindlib=libunwind"
    "-resource-dir=${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17"
    "-L${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17/lib/linux"
    "-L${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17/lib/linux/aarch64"
    "-L${CMAKE_SYSROOT}/usr/lib/aarch64-linux-android/34"
    "-L${CMAKE_SYSROOT}/usr/lib/aarch64-linux-android"
)
