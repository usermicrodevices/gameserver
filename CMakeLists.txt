cmake_minimum_required(VERSION 3.15)
project(GameServer3D)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(OpenGL REQUIRED)
find_package(glm REQUIRED)
find_package(asio REQUIRED)
find_package(nlohmann_json REQUIRED)

# Add GLM include directory
include_directories(${glm_INCLUDE_DIRS})

# Source files for 3D world system
set(WORLD_SOURCES
    src/game/GameLogic.cpp
    src/game/WorldChunk.cpp
    src/game/WorldGenerator.cpp
    src/game/NPCSystem.cpp
    src/game/MobSystem.cpp
    src/game/GameEntity.cpp
    src/game/CollisionSystem.cpp
    src/game/EntityManager.cpp
)

# Include directories
include_directories(
    include
    ${OPENGL_INCLUDE_DIR}
    ${glm_INCLUDE_DIRS}
)

# Main executable
add_executable(gameserver3d
    src/main.cpp
    ${WORLD_SOURCES}
    # ... existing source files ...
)

# Link libraries
target_link_libraries(gameserver3d
    ${OPENGL_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    asio::asio
    nlohmann_json::nlohmann_json
)

# Compiler flags
if(MSVC)
    target_compile_options(gameserver3d PRIVATE /W4)
else()
    target_compile_options(gameserver3d PRIVATE -Wall -Wextra -pedantic)
endif()

# Installation
install(TARGETS gameserver3d DESTINATION bin)
install(DIRECTORY config/ DESTINATION config)
install(DIRECTORY scripts/ DESTINATION scripts)
