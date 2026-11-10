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

# Option flags
option(SHADER_DEBUG_INFO "Compile shaders with debug information" OFF)

##################################################### Vulkan #####################################################
# Check for Vulkan SDK and DXC shipped alongside it
find_package(Vulkan QUIET OPTIONAL_COMPONENTS dxc)

# Vulkan SDK check
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR
        "Vulkan SDK not found!\n"
        "Please install the LunarG Vulkan SDK from:\n"
        "https://vulkan.lunarg.com/sdk/home\n"
        "Make sure the VULKAN_SDK environment variable is set."
    )
endif()

# Vulkan target version
# -> VK_EXT_descriptor_heap requires SDK 1.4.341.0 or newer.
set(VULKAN_TARGET_VERSION "1.4.341")

# Disassemble Vulkan target version
string(REPLACE "." ";" VULKAN_TARGET_VERSION_PARTS ${VULKAN_TARGET_VERSION})
list(GET VULKAN_TARGET_VERSION_PARTS 0 VULKAN_TARGET_VERSION_MAJOR)
list(GET VULKAN_TARGET_VERSION_PARTS 1 VULKAN_TARGET_VERSION_MINOR)
list(GET VULKAN_TARGET_VERSION_PARTS 2 VULKAN_TARGET_VERSION_PATCH)

# Check the Vulkan version
if (Vulkan_VERSION VERSION_LESS ${VULKAN_TARGET_VERSION})
    message(FATAL_ERROR
        "Found Vulkan SDK version ${Vulkan_VERSION}\n"
        "This app requires SDK version ${VULKAN_TARGET_VERSION} or newer!\n"
        "You can install a newer LunarG Vulkan SDK from:\n"
        "https://vulkan.lunarg.com/sdk/home\n"
        "or open Vulkan Configurator to update.\n"
        "Build with \"cmake --fresh <build-command>\" after Vulkan update."
    )
endif()

# DXC check
if(NOT TARGET Vulkan::dxc_exe)
    message(FATAL_ERROR
        "DXC executable was not found.\n"
        "Please install a Vulkan SDK that includes DXC."
    )
endif()

message(STATUS "Found Vulkan: ${Vulkan_VERSION}")
message(STATUS "Found DXC: ${Vulkan_dxc_EXECUTABLE}")
##################################################################################################################

##################################################### SHADER #####################################################
# Compile shaders with debug information
if(SHADER_DEBUG_INFO)
    message(STATUS "Compile shaders with debug information - ON")
    # Set the shader binary directory to debug
    set(SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/ShaderBin/Debug")
else()
    message(STATUS "Compile shaders with debug information - OFF")
    # Set the shader binary directory to release
    set(SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/ShaderBin/Release")
endif()

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
# - Debug options
#   -> Only added if option flag was toggled
if(SHADER_DEBUG_INFO)
    set(SPIRV_DEBUG_OPTIONS
        -Od                            # Disables optimizations
        -Zi                            # Enables debug information
        -fspv-debug=vulkan-with-source # Instructs the compiler to embed the source string in the DebugSource instruction. RenderDoc reads the high-level source from this instruction.
    )
else()
    set(SPIRV_DEBUG_OPTIONS)
endif()
#
# Output directory + filename
# - Binary
set(SHADER_OUTPUT_SPV "${SHADER_BIN_DIR}/StorageImageAccess.spv")
# - Assembly
set(SHADER_OUTPUT_ASM "${SHADER_BIN_DIR}/StorageImageAccess.asm")
#
# - Add compilation command
add_custom_command(
    OUTPUT ${SHADER_OUTPUT_SPV} ${SHADER_OUTPUT_ASM}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_BIN_DIR}" # Make directory for output
    COMMAND Vulkan::dxc_exe                                          # dxc
        ${SHADER_SRC_FILE}                                           # Shader source
        -E Main                                                      # Shader entry point
        -T cs_6_6                                                    # Shader type and target version
        # Vulkan 1.4 did not work as target with Vulkan SDK 1.4;
        # 1.3 was the highest available target
        -fspv-target-env=vulkan1.3                                   # Vulkan target version
        -spirv                                                       # Set SPIR-V compilation (also activates __spirv__ macro in HLSL)
        -Fo ${SHADER_OUTPUT_SPV}                                     # Compiled SPIR-V binary
        -Fc ${SHADER_OUTPUT_ASM}                                     # SPIR-V assembly
        ${SPIRV_DEBUG_OPTIONS}                                       # Debug options; see above
    DEPENDS ${SHADER_SRC_FILE}
    COMMENT "Compiling StorageImageAccess.hlsl to SPIR-V"
    VERBATIM
)
# - Add target
add_custom_target(ShaderCompilation ALL DEPENDS "${SHADER_OUTPUT_SPV}" "${SHADER_OUTPUT_ASM}")
##################################################################################################################

###################################################### APP #######################################################

# Add executable
add_executable(App App/Main.cpp)

# Set macros
target_compile_definitions(App PRIVATE
    # - Set API version macro
    #   -> For VkInstance creation, VkPhysicalDevice selection, and
    #      device feature activation.
    VULKAN_TARGET_API_VERSION=VK_MAKE_API_VERSION\(0,${VULKAN_TARGET_VERSION_MAJOR},${VULKAN_TARGET_VERSION_MINOR},${VULKAN_TARGET_VERSION_PATCH}\)
    # - Shader binary file path for shader loading
    SHADER_BIN_FILE_PATH="${SHADER_OUTPUT_SPV}"
)

# Link Vulkan
target_link_libraries(App PRIVATE Vulkan::Vulkan)

# In case we use Visual Studio (not VS Code!)
# - Set the startup project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT App)
# - Set the debuggers working directory to $(TargetDir) instead of $(ProjectDir).
#   $(ProjectDir) is the default for this setting in VS but then the Shaders are
#   not found if the program is run from inside VS.
set_property(TARGET App PROPERTY
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/Bin/$<CONFIG>
)
##################################################################################################################
