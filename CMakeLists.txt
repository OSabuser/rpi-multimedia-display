cmake_minimum_required(VERSION 3.16)
project(lift_indicator C)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

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

# ─── compile_commands.json для clangd ────────────────────────────────────────

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─── Domain-библиотека (platform-free, тестируется на хосте) ─────────────────

add_library(indicator_domain STATIC
    src/protocol/parser.c
    src/domain/floor.c
    src/domain/sound_map.c
    src/domain/state.c
    src/config/config.c
)
target_include_directories(indicator_domain PUBLIC src/)
apply_warnings(indicator_domain)

# ─── Transport-библиотека (uart.c — компилируется и на хосте, и на Pi) ───────
#
# uart.c зависит только от POSIX/termios — нет DispmanX, нет bcm_host.
# Сборка на хосте позволяет покрыть uart_process_rx() unit-тестами
# через pipe() без реального UART-устройства.

add_library(indicator_transport STATIC
    src/transport/uart.c
)
target_include_directories(indicator_transport PUBLIC src/)
target_link_libraries(indicator_transport PUBLIC indicator_domain)
apply_warnings(indicator_transport)

# Передать флаг в uart.c для компиляции uart_wrap_fd() при сборке тестов
if(INDICATOR_BUILD_TESTS)
    target_compile_definitions(indicator_transport PUBLIC INDICATOR_BUILD_TESTS)
endif()

# ─── Host тесты ──────────────────────────────────────────────────────────────

if(INDICATOR_BUILD_TESTS)
    enable_testing()

    add_library(unity STATIC tests/unity/unity.c)
    target_include_directories(unity PUBLIC tests/unity/)

    # Тесты domain-слоя (Фаза 1)
    set(INDICATOR_DOMAIN_TEST_SOURCES
        test_parser
        test_floor
        test_sound_map
        test_state
        test_config
    )
    foreach(TEST_NAME ${INDICATOR_DOMAIN_TEST_SOURCES})
        add_executable(${TEST_NAME} tests/${TEST_NAME}.c)
        target_link_libraries(${TEST_NAME} PRIVATE indicator_domain unity)
        target_include_directories(${TEST_NAME} PRIVATE src/)
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
        apply_warnings(${TEST_NAME})
    endforeach()

    # Тест transport-слоя (Фаза 2)
    add_executable(test_uart tests/test_uart.c)
    target_link_libraries(test_uart PRIVATE indicator_transport unity)
    target_include_directories(test_uart PRIVATE src/)
    add_test(NAME test_uart COMMAND test_uart)
    apply_warnings(test_uart)
endif()

# ─── Platform-зависимые таргеты (только Pi) ──────────────────────────────────

if(INDICATOR_PLATFORM_DISPMANX)
    add_subdirectory(platform/dispmanx)
    add_subdirectory(src_media_ingest)
    add_subdirectory(tools)

    add_executable(indicator
        src/main.c
        src/audio/audio.c
        src/player/video_player.c
        src/media/media_ipc.c
    )
    target_include_directories(indicator PRIVATE src)
    target_link_libraries(indicator
        PRIVATE
            indicator_domain
            indicator_transport
            dispmanx_renderer
            pthread
    )
    apply_warnings(indicator)


endif()