cmake_minimum_required(VERSION 3.14)
project(waflash_reflexed CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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

if(EMSCRIPTEN)
    # Target 0: Emscripten WebAssembly Target
    add_executable(waflash_web
        src/wasm_api.cpp
        src/swf_parser.cpp
        src/abc_parser.cpp
        src/input_manager.cpp
        src/avm2_vm.cpp
        src/display_list.cpp
        src/audio_mixer.cpp
        src/audio_stream_decoder.cpp
        src/vector_rasterizer.cpp
    )

    set_target_properties(waflash_web PROPERTIES
        OUTPUT_NAME "waflash_core"
        SUFFIX ".js"
    )

    target_include_directories(waflash_web PRIVATE include)
    target_link_libraries(waflash_web PRIVATE ZLIB::ZLIB)

    target_link_options(waflash_web PRIVATE
        "-sWASM=1"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=\"createWAFlashCore\""
        "-sALLOW_MEMORY_GROWTH=1"
        "-sUSE_ZLIB=1"
        "-sEXPORTED_FUNCTIONS=['_malloc','_free','_wasm_get_audio_samples']"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','getValue','HEAPU8','HEAP16','HEAPU32']"
        "-O3"
    )
endif()

# Target 1: Libretro Core Shared Library
add_library(libretro_flash SHARED src/core_stub.cpp src/swf_parser.cpp src/abc_parser.cpp src/input_manager.cpp src/avm2_vm.cpp src/display_list.cpp src/audio_mixer.cpp src/audio_stream_decoder.cpp src/vector_rasterizer.cpp)

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
add_executable(test_memory_map tests/test_memory_map.cpp src/core_stub.cpp src/swf_parser.cpp src/abc_parser.cpp src/input_manager.cpp src/avm2_vm.cpp src/display_list.cpp src/audio_mixer.cpp src/audio_stream_decoder.cpp src/vector_rasterizer.cpp)

target_include_directories(test_memory_map PRIVATE include)
target_link_libraries(test_memory_map PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(test_memory_map PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_memory_map PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_memory_map COMMAND test_memory_map)

# Target 3: Unit Test Executable - SWF Parser
add_executable(test_swf_parser tests/test_swf_parser.cpp src/swf_parser.cpp src/display_list.cpp src/audio_mixer.cpp src/audio_stream_decoder.cpp src/vector_rasterizer.cpp)

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
add_executable(test_video_frame tests/test_video_frame.cpp src/core_stub.cpp src/swf_parser.cpp src/abc_parser.cpp src/input_manager.cpp src/avm2_vm.cpp src/display_list.cpp src/audio_mixer.cpp src/audio_stream_decoder.cpp src/vector_rasterizer.cpp)

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
add_executable(test_timeline tests/test_timeline.cpp src/core_stub.cpp src/swf_parser.cpp src/abc_parser.cpp src/input_manager.cpp src/avm2_vm.cpp src/display_list.cpp src/audio_mixer.cpp src/audio_stream_decoder.cpp src/vector_rasterizer.cpp)

target_include_directories(test_timeline PRIVATE include)
target_link_libraries(test_timeline PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(test_timeline PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_timeline PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_timeline COMMAND test_timeline)

# Target 8: Unit Test Executable - AVM2 VM
add_executable(test_avm2_vm tests/test_avm2_vm.cpp src/avm2_vm.cpp src/abc_parser.cpp)

target_include_directories(test_avm2_vm PRIVATE include)

if(MSVC)
    target_compile_options(test_avm2_vm PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_avm2_vm PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_avm2_vm COMMAND test_avm2_vm)

# Target 9: Unit Test Executable - DisplayList
add_executable(test_display_list tests/test_display_list.cpp src/core_stub.cpp src/swf_parser.cpp src/abc_parser.cpp src/input_manager.cpp src/avm2_vm.cpp src/display_list.cpp src/audio_mixer.cpp src/audio_stream_decoder.cpp src/vector_rasterizer.cpp)

target_include_directories(test_display_list PRIVATE include)
target_link_libraries(test_display_list PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(test_display_list PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_display_list PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_display_list COMMAND test_display_list)

# Target 10: Unit Test Executable - Audio Subsystem
add_executable(test_audio tests/test_audio.cpp src/audio_mixer.cpp)

target_include_directories(test_audio PRIVATE include)

if(MSVC)
    target_compile_options(test_audio PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_audio PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_audio COMMAND test_audio)

# Target 11: Unit Test Executable - Serialization
add_executable(test_serialization tests/test_serialization.cpp src/core_stub.cpp src/swf_parser.cpp src/abc_parser.cpp src/input_manager.cpp src/avm2_vm.cpp src/display_list.cpp src/audio_mixer.cpp src/audio_stream_decoder.cpp src/vector_rasterizer.cpp)

target_include_directories(test_serialization PRIVATE include)
target_link_libraries(test_serialization PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(test_serialization PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_serialization PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_serialization COMMAND test_serialization)

# Target 12: Unit Test Executable - WASM API
add_executable(test_wasm_api tests/test_wasm_api.cpp src/wasm_api.cpp src/swf_parser.cpp src/abc_parser.cpp src/input_manager.cpp src/avm2_vm.cpp src/display_list.cpp src/audio_mixer.cpp src/audio_stream_decoder.cpp src/vector_rasterizer.cpp)

target_include_directories(test_wasm_api PRIVATE include)
target_link_libraries(test_wasm_api PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(test_wasm_api PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_wasm_api PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_wasm_api COMMAND test_wasm_api)

# Target 13: Unit Test Executable - Sound Stream
add_executable(test_sound_stream tests/test_sound_stream.cpp src/swf_parser.cpp src/display_list.cpp src/audio_mixer.cpp src/audio_stream_decoder.cpp src/vector_rasterizer.cpp)

target_include_directories(test_sound_stream PRIVATE include)
target_link_libraries(test_sound_stream PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(test_sound_stream PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_sound_stream PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_sound_stream COMMAND test_sound_stream)

# Target 14: Unit Test Executable - Sprite
add_executable(test_sprite tests/test_sprite.cpp src/swf_parser.cpp src/display_list.cpp src/audio_mixer.cpp src/audio_stream_decoder.cpp src/vector_rasterizer.cpp)

target_include_directories(test_sprite PRIVATE include)
target_link_libraries(test_sprite PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(test_sprite PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_sprite PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_sprite COMMAND test_sprite)

# Target 15: Unit Test Executable - Audio Stream
add_executable(test_audio_stream tests/test_audio_stream.cpp src/audio_stream_decoder.cpp src/swf_parser.cpp src/display_list.cpp src/audio_mixer.cpp src/vector_rasterizer.cpp)

target_include_directories(test_audio_stream PRIVATE include)
target_link_libraries(test_audio_stream PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(test_audio_stream PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_audio_stream PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_audio_stream COMMAND test_audio_stream)

# Target 16: Unit Test Executable - MovieClip & Scene Graph Hierarchy
add_executable(test_movieclip tests/test_movieclip.cpp src/swf_parser.cpp src/display_list.cpp src/audio_mixer.cpp src/audio_stream_decoder.cpp src/vector_rasterizer.cpp)

target_include_directories(test_movieclip PRIVATE include)
target_link_libraries(test_movieclip PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(test_movieclip PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_movieclip PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_movieclip COMMAND test_movieclip)

# Target 17: Unit Test Executable - Vector Tessellator & Scanline Rasterizer
add_executable(test_rasterizer tests/test_rasterizer.cpp src/vector_rasterizer.cpp)

target_include_directories(test_rasterizer PRIVATE include)

if(MSVC)
    target_compile_options(test_rasterizer PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_rasterizer PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_rasterizer COMMAND test_rasterizer)

# Target 18: Unit Test Executable - Per-Frame Timeline Command Queue
add_executable(test_frame_timeline tests/test_frame_timeline.cpp src/swf_parser.cpp src/display_list.cpp src/audio_mixer.cpp src/audio_stream_decoder.cpp src/vector_rasterizer.cpp)

target_include_directories(test_frame_timeline PRIVATE include)
target_link_libraries(test_frame_timeline PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(test_frame_timeline PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_frame_timeline PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_frame_timeline COMMAND test_frame_timeline)

# Target 19: Unit Test Executable - Ruffle Parity PlaceObject2 & Fixed-Point Matrix Decoding
add_executable(test_ruffle_parity tests/test_ruffle_parity.cpp src/swf_parser.cpp src/display_list.cpp src/audio_mixer.cpp src/audio_stream_decoder.cpp src/vector_rasterizer.cpp)

target_include_directories(test_ruffle_parity PRIVATE include)
target_link_libraries(test_ruffle_parity PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(test_ruffle_parity PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_ruffle_parity PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_ruffle_parity COMMAND test_ruffle_parity)

# Target 20: Integration Test Executable - Real-World Flash Benchmark (Hoshi Saga 1)
add_executable(test_hoshi_saga tests/test_hoshi_saga.cpp src/core_stub.cpp src/swf_parser.cpp src/abc_parser.cpp src/input_manager.cpp src/avm2_vm.cpp src/display_list.cpp src/audio_mixer.cpp src/audio_stream_decoder.cpp src/vector_rasterizer.cpp)

target_include_directories(test_hoshi_saga PRIVATE include)
target_link_libraries(test_hoshi_saga PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(test_hoshi_saga PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_hoshi_saga PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_hoshi_saga COMMAND test_hoshi_saga)

# Target 21: Integration Test Executable - Hoshi Saga 1 Gameplay & Button Interaction
add_executable(test_hoshi_gameplay tests/test_hoshi_gameplay.cpp src/core_stub.cpp src/swf_parser.cpp src/abc_parser.cpp src/input_manager.cpp src/avm2_vm.cpp src/display_list.cpp src/audio_mixer.cpp src/audio_stream_decoder.cpp src/vector_rasterizer.cpp)

target_include_directories(test_hoshi_gameplay PRIVATE include)
target_link_libraries(test_hoshi_gameplay PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(test_hoshi_gameplay PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_hoshi_gameplay PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()

add_test(NAME test_hoshi_gameplay COMMAND test_hoshi_gameplay)
