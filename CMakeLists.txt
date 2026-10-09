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

# ── gameswf AS1/AS2 engine (static, no OpenGL, no SDL, no stdio) ──────────────
set(GAMESWF_ROOT "${CMAKE_SOURCE_DIR}/third_party/gameswf/")

if(NOT EXISTS "${GAMESWF_ROOT}/gameswf/gameswf_action.cpp")
    message(STATUS "gameswf source files not found. Initializing git submodules...")
    find_package(Git REQUIRED)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE GAMESWF_SUBMODULE_RESULT
    )
    if(NOT GAMESWF_SUBMODULE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to initialize gameswf submodule")
    endif()
endif()

# Apply C++17 compatibility patches to gameswf files if needed
if(EXISTS "${GAMESWF_ROOT}/base/tu_math.h")
    file(READ "${GAMESWF_ROOT}/base/tu_math.h" TU_MATH_H_CONTENT)
    if(TU_MATH_H_CONTENT MATCHES "#define isfinite\\(x\\)")
        string(REPLACE
            "#ifndef isfinite"
            "#include <cmath>\n#undef isfinite\nusing std::isfinite;\n#if 0"
            TU_MATH_H_CONTENT "${TU_MATH_H_CONTENT}")
        file(WRITE "${GAMESWF_ROOT}/base/tu_math.h" "${TU_MATH_H_CONTENT}")
    endif()
endif()

if(EXISTS "${GAMESWF_ROOT}/base/utility.h")
    file(READ "${GAMESWF_ROOT}/base/utility.h" UTILITY_H_CONTENT)
    if(UTILITY_H_CONTENT MATCHES "inline float\tfmax")
        string(REPLACE
            "inline float\tfmax(float a, float b) { if (a < b) return b; else return a; }"
            "#include <cmath>\nusing std::fmax;"
            UTILITY_H_CONTENT "${UTILITY_H_CONTENT}")
        string(REPLACE
            "inline float\tfmin(float a, float b) { if (a < b) return a; else return b; }"
            "using std::fmin;"
            UTILITY_H_CONTENT "${UTILITY_H_CONTENT}")
        file(WRITE "${GAMESWF_ROOT}/base/utility.h" "${UTILITY_H_CONTENT}")
    endif()
    if(UTILITY_H_CONTENT MATCHES "switch\\(0\\)\\{case 0: case x:;\\}")
        string(REPLACE
            "#define compiler_assert(x)\tswitch(0){case 0: case x:;}"
            "#define compiler_assert(x)\tstatic_assert((x), \"compiler_assert failed\")"
            UTILITY_H_CONTENT "${UTILITY_H_CONTENT}")
        file(WRITE "${GAMESWF_ROOT}/base/utility.h" "${UTILITY_H_CONTENT}")
    endif()
endif()

if(EXISTS "${GAMESWF_ROOT}/gameswf/gameswf_environment.h")
    file(READ "${GAMESWF_ROOT}/gameswf/gameswf_environment.h" ENV_H_CONTENT)
    if(ENV_H_CONTENT MATCHES "private array<as_value>")
        string(REPLACE
            "struct vm_stack : private array<as_value>"
            "struct vm_stack : public array<as_value>"
            ENV_H_CONTENT "${ENV_H_CONTENT}")
        file(WRITE "${GAMESWF_ROOT}/gameswf/gameswf_environment.h" "${ENV_H_CONTENT}")
    endif()
endif()

if(EXISTS "${GAMESWF_ROOT}/base/container.h")
    file(READ "${GAMESWF_ROOT}/base/container.h" CONTAINER_H_CONTENT)
    if(CONTAINER_H_CONTENT MATCHES "unsigned int\thash_value = \\(unsigned int\\) compute_hash\\(key\\);")
        string(REPLACE
            "unsigned int\thash_value = (unsigned int) compute_hash(key);"
            "typedef size_t hash_val_t;\n\thash_val_t\thash_value = compute_hash(key);"
            CONTAINER_H_CONTENT "${CONTAINER_H_CONTENT}")
        string(REPLACE
            "entry(const T& key, const U& value, int next_in_chain, int hash_value)"
            "typedef size_t hash_val_t;\n\t\tentry(const T& key, const U& value, int next_in_chain, hash_val_t hash_value)"
            CONTAINER_H_CONTENT "${CONTAINER_H_CONTENT}")
        file(WRITE "${GAMESWF_ROOT}/base/container.h" "${CONTAINER_H_CONTENT}")
    endif()
endif()

if(EXISTS "${GAMESWF_ROOT}/gameswf/gameswf_sprite.cpp")
    file(READ "${GAMESWF_ROOT}/gameswf/gameswf_sprite.cpp" SPRITE_CPP_CONTENT)
    if(SPRITE_CPP_CONTENT MATCHES "m_init_actions_executed\\.resize\\(m_def->get_frame_count\\(\\)\\);")
        string(REPLACE
            "m_init_actions_executed.resize(m_def->get_frame_count());"
            "int fc = m_def->get_frame_count(); m_init_actions_executed.resize(fc > 0 ? fc : 1);"
            SPRITE_CPP_CONTENT "${SPRITE_CPP_CONTENT}")
    endif()
    if(SPRITE_CPP_CONTENT MATCHES "case M_TOTALFRAMES:")
        string(REPLACE
            "case M_TOTALFRAMES:\n\t\t{\n\t\t\t// number of frames.  Read only.\n\t\t\tint n = get_frame_count();\n\t\t\tif (n >= 0)\n\t\t\t{\n\t\t\t\tval->set_int(n);\n\t\t\t}\n\t\t\telse\n\t\t\t{\n\t\t\t\tval->set_undefined();\n\t\t\t}\n\t\t\treturn true;\n\t\t}\n\t\tcase M_FRAMESLOADED:\n\t\t{\n\t\t\tint n = get_loading_frame();\n\t\t\tif (n >= 0)\n\t\t\t{\n\t\t\t\tval->set_int(n);\n\t\t\t}\n\t\t\telse\n\t\t\t{\n\t\t\t\tval->set_undefined();\n\t\t\t}\n\t\t\treturn true;\n\t\t}"
            "case M_TOTALFRAMES:\n\t\t{\n\t\t\t// number of frames.  Read only.\n\t\t\tint n = get_frame_count();\n\t\t\tval->set_int(n > 0 ? n : 1);\n\t\t\treturn true;\n\t\t}\n\t\tcase M_FRAMESLOADED:\n\t\t{\n\t\t\tint n = get_loading_frame();\n\t\t\tint fc = get_frame_count();\n\t\t\tint loaded = n > fc ? n : fc;\n\t\t\tval->set_int(loaded > 0 ? loaded : 1);\n\t\t\treturn true;\n\t\t}"
            SPRITE_CPP_CONTENT "${SPRITE_CPP_CONTENT}")
    endif()
    file(WRITE "${GAMESWF_ROOT}/gameswf/gameswf_sprite.cpp" "${SPRITE_CPP_CONTENT}")
endif()

if(EXISTS "${GAMESWF_ROOT}/gameswf/gameswf_movie_def.cpp")
    file(READ "${GAMESWF_ROOT}/gameswf/gameswf_movie_def.cpp" MOVIE_DEF_CPP_CONTENT)
    if(MOVIE_DEF_CPP_CONTENT MATCHES "uint32\\tmovie_def_impl::get_file_bytes\\(\\)")
        string(REPLACE
            "uint32\tmovie_def_impl::get_file_bytes() const { return m_file_end_pos; }"
            "uint32\tmovie_def_impl::get_file_bytes() const { return m_file_end_pos > 0 ? m_file_end_pos : 1; }"
            MOVIE_DEF_CPP_CONTENT "${MOVIE_DEF_CPP_CONTENT}")
        file(WRITE "${GAMESWF_ROOT}/gameswf/gameswf_movie_def.cpp" "${MOVIE_DEF_CPP_CONTENT}")
    endif()
endif()

if(EXISTS "${GAMESWF_ROOT}/gameswf/gameswf_movie_def.h")
    file(READ "${GAMESWF_ROOT}/gameswf/gameswf_movie_def.h" MOVIE_DEF_H_CONTENT)
    if(MOVIE_DEF_H_CONTENT MATCHES "assert\\(m_loading_frame >= 0 && m_loading_frame <= m_frame_count\\);")
        string(REPLACE
            "assert(m_loading_frame >= 0 && m_loading_frame <= m_frame_count);"
            "//"
            MOVIE_DEF_H_CONTENT "${MOVIE_DEF_H_CONTENT}")
        file(WRITE "${GAMESWF_ROOT}/gameswf/gameswf_movie_def.h" "${MOVIE_DEF_H_CONTENT}")
    endif()
endif()

add_library(gameswf_core STATIC
    ${GAMESWF_ROOT}/gameswf/gameswf_action.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_dlist.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_impl.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_sprite.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_sprite_def.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_shape.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_stream.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_types.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_value.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_object.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_function.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_character.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_movie_def.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_root.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_player.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_render.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_sound.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_log.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_environment.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_sprite.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_button.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_canvas.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_morph2.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_tesselate.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_styles.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_disasm.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_listener.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_mutex.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_font.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_text.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_filters.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_abc.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_avm2.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_video_impl.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_array.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_boolean.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_broadcaster.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_class.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_color.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_color_transform.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_date.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_event.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_flash.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_geom.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_global.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_key.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_loadvars.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_math.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_matrix.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_mcloader.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_mouse.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_mouse_event.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_netconnection.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_netstream.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_number.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_point.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_selection.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_sharedobject.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_sound.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_string.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_textformat.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_transform.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_xml.cpp
    ${GAMESWF_ROOT}/gameswf/gameswf_as_classes/as_xmlsocket.cpp
    ${GAMESWF_ROOT}/base/tu_file.cpp
    ${GAMESWF_ROOT}/base/container.cpp
    ${GAMESWF_ROOT}/base/ear_clip_triangulate_float.cpp
    ${GAMESWF_ROOT}/base/ear_clip_triangulate_sint16.cpp
    ${GAMESWF_ROOT}/base/logger.cpp
    ${GAMESWF_ROOT}/base/membuf.cpp
    ${GAMESWF_ROOT}/base/tu_random.cpp
    ${GAMESWF_ROOT}/base/tu_timer.cpp
    ${GAMESWF_ROOT}/base/tu_loadlib.cpp
    ${GAMESWF_ROOT}/base/utf8.cpp
    ${GAMESWF_ROOT}/base/image.cpp
    ${GAMESWF_ROOT}/base/jpeg.cpp
    ${GAMESWF_ROOT}/base/zlib_adapter.cpp
)

target_include_directories(gameswf_core PUBLIC
    ${GAMESWF_ROOT}
    ${GAMESWF_ROOT}/gameswf
    ${GAMESWF_ROOT}/base
)

# Disable all platform backends — WASM/embedded only
target_compile_definitions(gameswf_core PUBLIC
    GAMESWF_NO_SDL=1
    GAMESWF_NO_OPENGL=1
    GAMESWF_NO_FREETYPE=1
    GAMESWF_NO_JPEG=1
    TU_CONFIG_LINK_TO_THREAD=0
    TU_CONFIG_LINK_TO_JPEGLIB=0
    TU_CONFIG_LINK_TO_ZLIB=1
    NOMINMAX=1
    WIN32_LEAN_AND_MEAN=1
    _USE_MATH_DEFINES=1
)

if(MSVC)
    target_compile_options(gameswf_core PRIVATE /W3 /O2 /GR-)
    target_compile_definitions(gameswf_core PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
        _TIME_T
    )
else()
    target_compile_options(gameswf_core PRIVATE -fPIC -w -O3 -fno-rtti)
endif()

target_link_libraries(gameswf_core PRIVATE ZLIB::ZLIB)

if(EMSCRIPTEN)
    # Target 0: Emscripten WebAssembly Target
    add_executable(waflash_web
        src/wasm_api.cpp
        src/gameswf_render_wasm.cpp
        src/gameswf_tu_file_mem.cpp
        # src/swf_parser.cpp
        # src/abc_parser.cpp
        src/input_manager.cpp
        # src/avm2_vm.cpp
        # src/display_list.cpp
        src/audio_mixer.cpp
        src/audio_stream_decoder.cpp
        src/vector_rasterizer.cpp
    )

    set_target_properties(waflash_web PROPERTIES
        OUTPUT_NAME "waflash_core"
        SUFFIX ".js"
    )

    target_include_directories(waflash_web PRIVATE include)
    target_link_libraries(waflash_web PRIVATE ZLIB::ZLIB gameswf_core)

    target_link_options(waflash_web PRIVATE
        "-sWASM=1"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=\"createWAFlashCore\""
        "-sALLOW_MEMORY_GROWTH=1"
        "-sUSE_ZLIB=1"
        "-sUSE_LIBJPEG=1"
        "-sEXPORTED_FUNCTIONS=['_malloc','_free','_wasm_get_audio_samples']"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','getValue','HEAPU8','HEAP16','HEAPU32']"
        "-O3"
    )

    target_compile_definitions(gameswf_core PUBLIC
        TU_CONFIG_LINK_TO_JPEGLIB=1
        TU_CONFIG_LINK_TO_ZLIB=1
    )

    target_compile_options(gameswf_core INTERFACE
        "-sUSE_LIBJPEG=1"
        "-sUSE_ZLIB=1"
    )
    target_compile_options(gameswf_core PRIVATE
        "-sUSE_LIBJPEG=1"
        "-sUSE_ZLIB=1"
    )
    target_link_options(gameswf_core INTERFACE
        "-sUSE_LIBJPEG=1"
        "-sUSE_ZLIB=1"
    )
endif()

# Target 1: Libretro Core Shared Library
add_library(libretro_flash SHARED
    src/core_stub.cpp
    src/gameswf_render_wasm.cpp
    src/gameswf_tu_file_mem.cpp
    src/swf_parser.cpp
    # src/abc_parser.cpp
    src/input_manager.cpp
    src/avm2_vm.cpp
    src/display_list.cpp
    src/audio_mixer.cpp
    src/audio_stream_decoder.cpp
    src/vector_rasterizer.cpp
)

set_target_properties(libretro_flash PROPERTIES
    OUTPUT_NAME "libretro_flash"
    PREFIX ""
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_include_directories(libretro_flash PUBLIC include)
target_link_libraries(libretro_flash PRIVATE ZLIB::ZLIB gameswf_core)

if(MSVC)
    target_compile_options(libretro_flash PRIVATE /W4 /O2 /EHs-c- /GR-)
else()
    target_compile_options(libretro_flash PRIVATE -fPIC -Wall -Wextra -O3 -fno-exceptions -fno-rtti)
endif()

if(APPLE)
    target_link_options(libretro_flash PRIVATE "-Wl,-undefined,dynamic_lookup")
endif()

# Unit Test Executable - Input Subsystem
add_executable(test_input tests/test_input.cpp src/input_manager.cpp)
target_include_directories(test_input PRIVATE include)
if(MSVC)
    target_compile_options(test_input PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_input PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()
add_test(NAME test_input COMMAND test_input)

# Unit Test Executable - Audio Subsystem
add_executable(test_audio tests/test_audio.cpp src/audio_mixer.cpp)
target_include_directories(test_audio PRIVATE include)
if(MSVC)
    target_compile_options(test_audio PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_audio PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()
add_test(NAME test_audio COMMAND test_audio)

# Unit Test Executable - Vector Tessellator & Scanline Rasterizer
add_executable(test_rasterizer tests/test_rasterizer.cpp src/vector_rasterizer.cpp)
target_include_directories(test_rasterizer PRIVATE include)
if(MSVC)
    target_compile_options(test_rasterizer PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_rasterizer PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()
add_test(NAME test_rasterizer COMMAND test_rasterizer)

# Unit Test Executable - WASM API
add_executable(test_wasm_api
    tests/test_wasm_api.cpp
    src/wasm_api.cpp
    src/gameswf_render_wasm.cpp
    src/gameswf_tu_file_mem.cpp
    src/input_manager.cpp
    src/audio_mixer.cpp
    src/audio_stream_decoder.cpp
    src/vector_rasterizer.cpp
)
target_include_directories(test_wasm_api PRIVATE include)
target_link_libraries(test_wasm_api PRIVATE ZLIB::ZLIB gameswf_core)
if(MSVC)
    target_compile_options(test_wasm_api PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_wasm_api PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()
add_test(NAME test_wasm_api COMMAND test_wasm_api)

# Unit Test Executable - Runtime Patches
add_executable(test_runtime_patches
    tests/test_runtime_patches.cpp
    src/swf_parser.cpp
    src/display_list.cpp
    src/audio_mixer.cpp
    src/audio_stream_decoder.cpp
    src/vector_rasterizer.cpp
    src/wasm_api.cpp
    src/gameswf_render_wasm.cpp
    src/gameswf_tu_file_mem.cpp
    src/input_manager.cpp
)
target_include_directories(test_runtime_patches PRIVATE include)
target_link_libraries(test_runtime_patches PRIVATE ZLIB::ZLIB gameswf_core)
if(MSVC)
    target_compile_options(test_runtime_patches PRIVATE /W4 /EHs-c- /GR-)
else()
    target_compile_options(test_runtime_patches PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
endif()
add_test(NAME test_runtime_patches COMMAND test_runtime_patches)
