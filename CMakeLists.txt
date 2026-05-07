cmake_minimum_required(VERSION 3.20)
project(MobileCAD VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler-specific warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# --- Shader Compilation (host toolchain, runs at build time) ---
# This section is shared between host and Android builds

# Prefer slangc (for .slang files), fallback to glslangValidator
find_program(SLANG slangc)
find_program(GLSLANG_VALIDATOR glslangValidator)

set(SHADER_COMPILER "")
set(SHADER_COMPILER_TYPE "")

if(SLANG)
    message(STATUS "Found slangc: ${SLANG} - will use for Slang shader compilation")
    set(SHADER_COMPILER "${SLANG}")
    set(SHADER_COMPILER_TYPE "slangc")
elseif(GLSLANG_VALIDATOR)
    message(STATUS "Found glslangValidator: ${GLSLANG_VALIDATOR} - will use for GLSL shader compilation")
    set(SHADER_COMPILER "${GLSLANG_VALIDATOR}")
    set(SHADER_COMPILER_TYPE "glslang")
else()
    message(WARNING "No shader compiler found (slangc or glslangValidator).")
    set(SHADER_COMPILER "")
endif()

# Shader source files
set(SHADER_SOURCES
    src/rendering/shaders/simple.vert.slang
    src/rendering/shaders/simple.frag.slang
)
set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)

# Shader compilation rules
set(SHADER_OUTPUTS "")
if(SHADER_COMPILER)
    foreach(SHADER ${SHADER_SOURCES})
        set(SHADER_ABS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}")
        if(EXISTS ${SHADER_ABS})
            get_filename_component(SHADER_EXT ${SHADER} EXT)
            get_filename_component(SHADER_FULL_NAME ${SHADER} NAME_WE)
            string(REPLACE ".slang" "" SHADER_BASE "${SHADER_FULL_NAME}")
            set(OUTPUT ${SHADER_OUTPUT_DIR}/${SHADER_BASE}.spv)

            # Determine shader stage from file extension
            if(SHADER_EXT STREQUAL ".vert.slang")
                set(STAGE vertex)
            elseif(SHADER_EXT STREQUAL ".frag.slang")
                set(STAGE fragment)
            elseif(SHADER_EXT STREQUAL ".comp.slang")
                set(STAGE compute)
            else()
                set(STAGE vertex)
            endif()

            if(SHADER_COMPILER_TYPE STREQUAL "slangc")
                add_custom_command(
                    OUTPUT ${OUTPUT}
                    COMMAND ${SHADER_COMPILER} ${SHADER_ABS} -target spirv -entry main -stage ${STAGE} -o ${OUTPUT}
                    DEPENDS ${SHADER_ABS}
                    COMMENT "Compiling ${SHADER} to SPIR-V"
                )
            elseif(SHADER_COMPILER_TYPE STREQUAL "glslang")
                add_custom_command(
                    OUTPUT ${OUTPUT}
                    COMMAND ${SHADER_COMPILER} -V -S ${STAGE} -o ${OUTPUT} "${SHADER_ABS}"
                    DEPENDS ${SHADER_ABS}
                    COMMENT "Compiling ${SHADER} to SPIR-V"
                )
            endif()
            list(APPEND SHADER_OUTPUTS ${OUTPUT})
        else()
            message(WARNING "Shader source not found: ${SHADER_ABS}")
        endif()
    endforeach()
endif()

if(SHADER_OUTPUTS)
    add_custom_target(compile_shaders ALL DEPENDS ${SHADER_OUTPUTS})
endif()

# The Android wrapper CMakeLists (in android/app/) adds the executable target
# with NDK-specific settings. This root file only provides shared shader logic.
