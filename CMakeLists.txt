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
    src/config/config.c
    src/protocol/parser.c
    src/domain/floor.c
    src/domain/sound_map.c
    src/domain/state.c
)
target_include_directories(indicator_domain
    PUBLIC
        src
        third_party/inih
)
target_link_libraries(indicator_domain PRIVATE inih)
apply_warnings(indicator_domain)

# ─── Host тесты ──────────────────────────────────────────────────────────────

if(INDICATOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
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
