cmake_minimum_required(VERSION 3.14)
project(terminal_ai_test CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies
find_package(CURL REQUIRED)
find_package(Readline) # Custom find module might be needed, or just link library

# If Readline is not found via standard module, we might need to link it manually
# For now, let's assume standard paths or use pkg-config if available.
# But simpler: just link "readline"

# nlohmann_json
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

add_executable(terminal_ai_test src/main.cpp)

target_link_libraries(terminal_ai_test PRIVATE 
    CURL::libcurl
    nlohmann_json::nlohmann_json
    readline
)
