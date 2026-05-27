cmake_minimum_required(VERSION 3.16)
project(lift_indicator C)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ─── Опции ───────────────────────────────────────────────────────────────────

option(INDICATOR_PLATFORM_DISPMANX  "Собрать DispmanX backend (только Pi)"  OFF)
option(INDICATOR_BUILD_TESTS        "Собрать host unit-тесты"               ON)
option(INDICATOR_BUILD_INGEST       "Собрать media_ingest демон"             ON)
option(INDICATOR_ENABLE_ASAN        "AddressSanitizer"                       OFF)
option(INDICATOR_ENABLE_UBSAN       "UndefinedBehaviorSanitizer"             OFF)
option(INDICATOR_WARNINGS_AS_ERRORS "Предупреждения = ошибки"                OFF)

# ─── Предупреждения компилятора ───────────────────────────────────────────────

include(cmake/Warnings.cmake)

# ─── Санитайзеры (только для host Debug) ─────────────────────────────────────

if(INDICATOR_ENABLE_ASAN OR INDICATOR_ENABLE_UBSAN)
    include(cmake/Sanitizers.cmake)
endif()

# ─── Экспорт compile_commands.json для clangd ────────────────────────────────

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─── Third-party ─────────────────────────────────────────────────────────────

add_subdirectory(third_party/inih)

# ─── Domain-библиотека (platform-free, тестируется на хосте) ─────────────────

add_library(indicator_domain STATIC
    src/protocol/parser.c
    src/domain/floor.c
    src/domain/sound_map.c
    src/domain/state.c
    src/config/config.c
)
target_include_directories(indicator_domain PUBLIC src/)

# ─── Host тесты ──────────────────────────────────────────────────────────────

if(INDICATOR_BUILD_TESTS)
    enable_testing()
 
    # Unity — единственный vendored файл, компилируем один раз
    add_library(unity STATIC tests/unity/unity.c)
    target_include_directories(unity PUBLIC tests/unity/)
 
    # Список тест-файлов → отдельный executable + CTest entry
    set(INDICATOR_TEST_SOURCES
        test_parser
        test_floor
        test_sound_map
        test_state
        test_config
    )
 
    foreach(TEST_NAME ${INDICATOR_TEST_SOURCES})
        add_executable(${TEST_NAME} tests/${TEST_NAME}.c)
        target_link_libraries(${TEST_NAME} PRIVATE indicator_domain unity)
        target_include_directories(${TEST_NAME} PRIVATE src/)
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
 
    
        apply_warnings(${TEST_NAME})
    endforeach()
endif()

# ─── Platform-зависимые таргеты (только Pi) ──────────────────────────────────

if(INDICATOR_PLATFORM_DISPMANX)
    add_subdirectory(platform/dispmanx)
    add_subdirectory(src_media_ingest)

    add_executable(indicator
        src/main.c
        src/transport/uart.c
        src/audio/audio.c
        src/player/video_player.c
        src/media/media_ipc.c
    )
    target_include_directories(indicator PRIVATE src)
    target_link_libraries(indicator
        PRIVATE
            indicator_domain
            dispmanx_renderer
            pthread
    )
    apply_warnings(indicator)
endif()
