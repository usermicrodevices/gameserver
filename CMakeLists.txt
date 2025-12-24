cmake_minimum_required(VERSION 3.15)
project(GameServer VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ASIO standalone
add_library(asio INTERFACE)
target_include_directories(asio INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/asio/asio/include
)

# nlohmann-json
find_package(nlohmann_json 3.9.1 REQUIRED)

# PostgreSQL
find_package(PostgreSQL REQUIRED)

# spdlog for logging
find_package(spdlog 1.9.0 REQUIRED)

# Add Python support
find_package(Python3 COMPONENTS Development Interpreter REQUIRED)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${Python3_INCLUDE_DIRS})

# Sources
set(SOURCES
    src/main.cpp
    src/config/ConfigManager.cpp
    src/database/DatabasePool.cpp
    src/database/CitusClient.cpp
    src/network/GameSession.cpp
    src/network/GameServer.cpp
    src/network/ConnectionManager.cpp
    src/process/ProcessPool.cpp
    src/logging/Logger.cpp
    src/game/GameLogic.cpp
    src/game/PlayerManager.cpp
)

# Executable
add_executable(game_server ${SOURCES})

# Link libraries
target_link_libraries(game_server
    asio
    nlohmann_json::nlohmann_json
    PostgreSQL::PostgreSQL
    spdlog::spdlog
    pthread
    ${Python3_LIBRARIES}
)

# Install
install(TARGETS game_server DESTINATION bin)
install(DIRECTORY config/ DESTINATION config)
install(DIRECTORY scripts/ DESTINATION scripts)
