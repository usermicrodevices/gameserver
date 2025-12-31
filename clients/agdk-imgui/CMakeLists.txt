cmake_minimum_required(VERSION 3.10)
project(AndroidGameClient)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Android specific
set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK})
set(CMAKE_ANDROID_API 24)
set(CMAKE_ANDROID_ARCH_ABI arm64-v8a)
set(CMAKE_ANDROID_STL_TYPE c++_shared)

# Include directories
include_directories(
    src
    ${CMAKE_ANDROID_NDK}/sources/third_party/vulkan/src/include
    ${CMAKE_ANDROID_NDK}/sources/android/native_app_glue
    ${CMAKE_ANDROID_NDK}/sources/cxx-stl/llvm-libc++/include
)

# Find packages
find_package(OpenGL REQUIRED)
find_package(EGL REQUIRED)

# Add ASIO (header-only)
add_library(asio INTERFACE)
target_include_directories(asio INTERFACE
    ${CMAKE_SOURCE_DIR}/third_party/asio/include
)

# Add nlohmann_json
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    ${CMAKE_SOURCE_DIR}/third_party/json/include
)

# Add glm
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE
    ${CMAKE_SOURCE_DIR}/third_party/glm
)

# Add ImGui
add_subdirectory(third_party/imgui)

# Source files
set(SOURCES
    src/AndroidMain.cpp
    src/GameClient.cpp
    src/NetworkClient.cpp
    src/Renderer.cpp
    src/InputHandler.cpp
    src/UIManager.cpp
    src/ShaderProgram.cpp
    src/TextureManager.cpp
    src/GameState.cpp
    src/EntityState.cpp
)

# Headers
set(HEADERS
    include/GameClient.hpp
    include/NetworkClient.hpp
    include/Renderer.hpp
    include/InputHandler.hpp
    include/UIManager.hpp
    include/ShaderProgram.hpp
    include/TextureManager.hpp
    include/GameState.hpp
    include/EntityState.hpp
    include/WorldChunk.hpp
    include/Mesh.hpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Link libraries
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    android
    EGL
    GLESv3
    log
    game-activity
    gametextinput
    asio
    nlohmann_json
    glm
    imgui
)

# Compiler flags
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -Werror
    -O2
    -ffast-math
    -funroll-loops
)

# Android specific properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    ANDROID_ABI arm64-v8a
    ANDROID_PLATFORM android-24
    ANDROID_STL c++_shared
)

# Copy assets
file(COPY assets DESTINATION ${CMAKE_BINARY_DIR})