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
