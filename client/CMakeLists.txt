cmake_minimum_required(VERSION 3.15)
project(GameServerClient)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(wxWidgets REQUIRED COMPONENTS core base gl aui)
find_package(OpenGL REQUIRED)
find_package(Python3 COMPONENTS Development REQUIRED)
find_package(asio REQUIRED)
find_package(glm REQUIRED)
find_package(nlohmann_json REQUIRED)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${wxWidgets_INCLUDE_DIRS})
include_directories(${OpenGL_INCLUDE_DIRS})
include_directories(${Python3_INCLUDE_DIRS})

# Source files
set(CLIENT_SOURCES
    src/main.cpp
    src/ClientApp.cpp
    src/ClientFrame.cpp
    src/GLCanvas.cpp
    src/NetworkClient.cpp
    src/GameWorld.cpp
    src/PythonScriptManager.cpp
)

# Header files
set(CLIENT_HEADERS
    include/client/GameClient.h
    include/client/Player.h
    include/client/Camera.h
    include/client/InputManager.h
    include/client/RenderSystem.h
    include/client/UIComponents.h
    include/client/ScriptBindings.h
    include/python/PythonWrapper.h
    include/python/GameModule.h
    include/python/ScriptEvents.h
)

# Create executable
add_executable(GameServerClient ${CLIENT_SOURCES} ${CLIENT_HEADERS})

# Link libraries
target_link_libraries(GameServerClient
    ${wxWidgets_LIBRARIES}
    ${OpenGL_LIBRARIES}
    ${Python3_LIBRARIES}
    asio::asio
    glm::glm
    nlohmann_json::nlohmann_json
)

# Copy Python scripts
file(COPY scripts DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY resources DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# Add definitions for Windows
if(WIN32)
    target_compile_definitions(GameServerClient PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(GameServerClient ws2_32)
endif()

# Set output directory
set_target_properties(GameServerClient PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Install target
install(TARGETS GameServerClient DESTINATION bin)
install(DIRECTORY scripts/ DESTINATION scripts)
install(DIRECTORY resources/ DESTINATION resources)