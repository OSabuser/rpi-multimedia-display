# tools/CMakeLists.txt
# Диагностические утилиты — собираются только на Pi (нужен реальный UART).


if(INDICATOR_PLATFORM_DISPMANX)

    add_executable(uart_rx_dump uart_rx_dump.c)
    target_include_directories(uart_rx_dump PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(uart_rx_dump PRIVATE indicator_transport)
    apply_warnings(uart_rx_dump)

endif()