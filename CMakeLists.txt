cmake_minimum_required(VERSION 3.14)
project(waflash_reflexed CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find ZLIB dependency or fallback to FetchContent
find_package(ZLIB QUIET)

if(NOT ZLIB_FOUND)
    include(FetchContent)
    message(STATUS "ZLIB not found on system. Fetching zlib via FetchContent...")
    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG        v1.3.1
    )
    # Disable zlib examples and tests
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zlib)

    if(TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    elseif(TARGET zlib AND NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlib)
    endif()
endif()

# Enable testing
enable_testing()

# Target 1: Libretro Core Shared Library
add_library(libretro_flash SHARED src/core_stub.cpp src/swf_parser.cpp src/abc_parser.cpp src/input_manager.cpp)

set_target_properties(libretro_flash PROPERTIES
    OUTPUT_NAME "libretro_flash"
    PREFIX ""
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_include_directories(libretro_flash PUBLIC include)
target_link_libraries(libretro_flash PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(libretro_flash PRIVATE /W4 /O2 /EHs-c- /GR-)
else()
    target_compile_options(libretro_flash PRIVATE -fPIC -Wall -Wextra -O3 -fno-exceptions -fno-rtti)
endif()

# Target 2: Unit Test Executable - Memory Map
add_executable(test_memory_map tests/test_memory_map.cpp src/core_stub.cpp src/swf_parser.cpp src/abc_parser.cpp src/input_manager.cpp)

target_include_directories(test_memory_map PRIVATE include)
target_link_libraries(test_memory_map PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(test_memory_map PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_memory_map PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_memory_map COMMAND test_memory_map)

# Target 3: Unit Test Executable - SWF Parser
add_executable(test_swf_parser tests/test_swf_parser.cpp src/swf_parser.cpp)

target_include_directories(test_swf_parser PRIVATE include)
target_link_libraries(test_swf_parser PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(test_swf_parser PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_swf_parser PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_swf_parser COMMAND test_swf_parser)

# Target 4: Unit Test Executable - ABC Parser
add_executable(test_abc_parser tests/test_abc_parser.cpp src/abc_parser.cpp)

target_include_directories(test_abc_parser PRIVATE include)

if(MSVC)
    target_compile_options(test_abc_parser PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_abc_parser PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_abc_parser COMMAND test_abc_parser)

# Target 5: Unit Test Executable - Video Frame
add_executable(test_video_frame tests/test_video_frame.cpp src/core_stub.cpp src/swf_parser.cpp src/abc_parser.cpp src/input_manager.cpp)

target_include_directories(test_video_frame PRIVATE include)
target_link_libraries(test_video_frame PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(test_video_frame PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_video_frame PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_video_frame COMMAND test_video_frame)

# Target 6: Unit Test Executable - Input Subsystem
add_executable(test_input tests/test_input.cpp src/input_manager.cpp)

target_include_directories(test_input PRIVATE include)

if(MSVC)
    target_compile_options(test_input PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_input PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_input COMMAND test_input)

# Target 7: Unit Test Executable - Timeline Stepper
add_executable(test_timeline tests/test_timeline.cpp src/core_stub.cpp src/swf_parser.cpp src/abc_parser.cpp src/input_manager.cpp)

target_include_directories(test_timeline PRIVATE include)
target_link_libraries(test_timeline PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(test_timeline PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_timeline PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_timeline COMMAND test_timeline)
