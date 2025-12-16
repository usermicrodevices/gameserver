cmake_minimum_required(VERSION 3.15)
project(GameNetwork)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Python
find_package(Python3 COMPONENTS Development REQUIRED)

# ASIO (standalone)
add_library(asio INTERFACE)
target_include_directories(asio INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio/asio/include)

# Main library
add_library(game_network STATIC
    src/binary_protocol.cpp
    src/python_embedder.cpp
    src/game_server.cpp
    src/game_client.cpp
)

target_include_directories(game_network PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${Python3_INCLUDE_DIRS}
)

target_link_libraries(game_network PUBLIC
    asio
    ${Python3_LIBRARIES}
)

# Examples
add_executable(example_server examples/example_server.cpp)
target_link_libraries(example_server game_network)

add_executable(example_client examples/example_client.cpp)
target_link_libraries(example_client game_network)

# Copy Python scripts to build directory
file(COPY scripts DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
