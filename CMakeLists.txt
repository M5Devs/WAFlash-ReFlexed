cmake_minimum_required(VERSION 3.14)
project(waflash_reflexed CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable testing
enable_testing()

# Target 1: Libretro Core Shared Library
add_library(libretro_flash SHARED src/core_stub.cpp)

set_target_properties(libretro_flash PROPERTIES
    OUTPUT_NAME "libretro_flash"
    PREFIX ""
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_include_directories(libretro_flash PUBLIC include)

if(MSVC)
    target_compile_options(libretro_flash PRIVATE /W4 /O2 /EHs-c- /GR-)
else()
    target_compile_options(libretro_flash PRIVATE -fPIC -Wall -Wextra -O3 -fno-exceptions -fno-rtti)
endif()

# Target 2: Unit Test Executable
add_executable(test_memory_map tests/test_memory_map.cpp src/core_stub.cpp)

target_include_directories(test_memory_map PRIVATE include)

if(MSVC)
    target_compile_options(test_memory_map PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_memory_map PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_memory_map COMMAND test_memory_map)
