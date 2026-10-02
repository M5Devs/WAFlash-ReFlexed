cmake_minimum_required(VERSION 3.14)
project(waflash_reflexed CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find ZLIB dependency
find_package(ZLIB REQUIRED)

# Enable testing
enable_testing()

# Target 1: Libretro Core Shared Library
add_library(libretro_flash SHARED src/core_stub.cpp src/swf_parser.cpp)

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
add_executable(test_memory_map tests/test_memory_map.cpp src/core_stub.cpp src/swf_parser.cpp)

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
