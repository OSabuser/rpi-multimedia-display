# tests/CMakeLists.txt

# Unity test framework (vendored)
add_library(unity STATIC unity/unity.c)
target_include_directories(unity PUBLIC unity)

# ─── Вспомогательная функция для добавления тестов ───────────────────────────

function(add_indicator_test name sources)
    add_executable(${name} ${sources})
    target_link_libraries(${name} PRIVATE unity indicator_domain)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    apply_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# ─── Тесты ───────────────────────────────────────────────────────────────────

# Добавляются по мере написания:
add_indicator_test(test_parser    test_parser.c)
add_indicator_test(test_floor     test_floor.c)
add_indicator_test(test_state     test_state.c)
add_indicator_test(test_sound_map test_sound_map.c)
add_indicator_test(test_config    test_config.c)
