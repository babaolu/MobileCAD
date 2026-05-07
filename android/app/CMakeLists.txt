cmake_minimum_required(VERSION 3.20)
project(mobilecad)

# --- NDK CMakeLists: Android-specific configuration ---

# Find Vulkan (Android NDK ships headers via platform sysroot or ndk-bundle)
find_package(Vulkan REQUIRED)

# Source files
set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/CADApp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/VulkanContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/Swapchain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering/Pipeline.cpp
)

# Shared library (loaded by NativeActivity)
add_library(mobilecad SHARED ${SOURCES})

target_include_directories(mobilecad PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# NDK libraries: android, log, android_native_app_glue, vulkan
find_library(android-lib android)
find_library(log-lib log)

target_link_libraries(mobilecad PRIVATE
    ${android-lib}
    ${log-lib}
    android_native_app_glue
    Vulkan::Vulkan
)

# Shader compilation: pull in the root shader compilation logic
add_dependencies(mobilecad compile_shaders)

# Copy compiled shaders to APK assets directory
add_custom_command(TARGET mobilecad POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/src/main/assets/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/shaders/*.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main/assets/shaders/
    COMMENT "Copying compiled shaders to APK assets"
)
