cmake_minimum_required(VERSION 3.15)
project(minitop CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_compile_definitions(WINVER=0x0600 _WIN32_WINNT=0x0600)
endif()

# Source files for the main application
set(MINITOP_SOURCES
    src/main.cpp
    src/proc_parser.cpp
    src/keyboard.cpp
)

# Header search paths
include_directories(include)

# Create main executable
add_executable(minitop ${MINITOP_SOURCES})

# Target-specific configuration for Windows vs Linux
if(WIN32)
    # Add mocking source for Windows execution
    target_sources(minitop PRIVATE src/proc_mock.cpp)
    # Link Windows Process Status API library (for memory stats)
    target_link_libraries(minitop PRIVATE psapi)
else()
    # On Linux, compile with thread support (pthread)
    find_package(Threads REQUIRED)
    target_link_libraries(minitop PRIVATE Threads::Threads)
endif()

# Tests
add_executable(test_parser tests/test_parser.cpp src/proc_parser.cpp)
if(WIN32)
    target_sources(test_parser PRIVATE src/proc_mock.cpp)
    target_link_libraries(test_parser PRIVATE psapi)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(test_parser PRIVATE Threads::Threads)
endif()
target_include_directories(test_parser PRIVATE include)
