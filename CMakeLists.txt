# Set minimum CMake version
cmake_minimum_required(VERSION 4.2)

# Set project
project(
    VkDescriptorHeapStorageImageAccess
    VERSION 1.0.0
    LANGUAGES C CXX
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set output directory for executable
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Bin/Release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/Bin/Debug)

##################################################### SHADER #####################################################

# Set shader source file
set(SHADER_SRC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/Shader/StorageImageAccess.hlsl)

# Ensure Visual Studio does not try to compile HLSL shader using
# its integrated HLSL compiler. We handle that ourselves with the
# DXC compiler.
set_source_files_properties(
    ${SHADER_SRC_FILE}
    PROPERTIES
        VS_TOOL_OVERRIDE "None"
        HEADER_FILE_ONLY TRUE
)

# Add interface library for shader
add_library(Shader INTERFACE ${SHADER_SRC_FILE})

# Compile shader using DXC shipped alongide Vulkan
#
# - Check for DXC
find_package(Vulkan REQUIRED COMPONENTS dxc)
if(NOT TARGET Vulkan::dxc_exe)
    message(FATAL_ERROR "DXC executable was not found in Vulkan SDK.")
endif()
# - Set shader output
set(shader_output "${CMAKE_CURRENT_BINARY_DIR}/Shader/StorageImageAccess.spv")
# - Add compilation command
add_custom_command(
    OUTPUT "${shader_output}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/Shader"
    COMMAND Vulkan::dxc_exe
            -spirv
            # Vulkan 1.4 did not work as target with Vulkan SDK 1.4; 1.3 was the highest
            # available target
            -fspv-target-env=vulkan1.3
            -T cs_6_6
            -E Main
            -Fo "${shader_output}"
            "${SHADER_SRC_FILE}"
    DEPENDS
        "${SHADER_SRC_FILE}"
        Vulkan::dxc_exe
    COMMENT "Compiling StorageImageAccess.hlsl to SPIR-V"
    VERBATIM
)
# - Add target
add_custom_target(ShaderCompilation ALL DEPENDS "${shader_output}")

###################################################### APP #######################################################

# TODO: Add Vulkan check
# TODO: Link Vulkan
# TODO: Add shader debug information
# TODO: Must adjust shader output directory

# Add executable
add_executable(App App/Main.cpp)

# In case we use Visual Studio (not VS Code!)
# - Set the startup project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT App)
# - Set the debuggers working directory to $(TargetDir) instead of $(ProjectDir).
#   $(ProjectDir) is the default for this setting in VS but then the Shaders are
#   not found if the program is run from inside VS.
set_property(TARGET App PROPERTY
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/Bin/$<CONFIG>
)
