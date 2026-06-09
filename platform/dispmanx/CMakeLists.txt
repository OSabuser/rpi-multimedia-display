# platform/dispmanx/CMakeLists.txt
#
# Зависимости:
#   bcm_host  — /opt/vc (в Docker-образе)
#   libpng16  — динамически: build-env/pi-sysroot/usr/lib/arm-linux-gnueabihf/libpng16.so
#   libz      — динамически: build-env/pi-sysroot/usr/lib/arm-linux-gnueabihf/libz.so
#
# ВАЖНО: libpng16.a из Buster/armhf скомпилирована с -march=armv7-a (Thumb-2).
# Статическая линковка даёт SIGSEGV на ARMv6 (Pi Zero W) в .init_array до main().
# Используем .so: линкуется здесь, загружается Pi-динкером на ARMv6 runtime.
#
# ВАЖНО: CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY ограничивает find_library() → /opt/vc.
# Для png/z используем строки "-L..." и "-l..." напрямую в target_link_libraries —
# CMake передаёт их линкеру без прохода через find_library.

set(PI_USR_SYSROOT "${CMAKE_SOURCE_DIR}/build-env/pi-sysroot/usr")

# Проверка: сообщить что делать если sysroot не подготовлен
if(NOT EXISTS "${PI_USR_SYSROOT}/include/png.h")
    message(FATAL_ERROR
        "\npng.h not found: ${PI_USR_SYSROOT}/include/png.h"
        "\nRun: just pi::fetch-png-sysroot\n"
    )
endif()
if(NOT EXISTS "${PI_USR_SYSROOT}/lib/arm-linux-gnueabihf/libpng16.so")
    message(FATAL_ERROR
        "\nlibpng16.so not found: ${PI_USR_SYSROOT}/lib/arm-linux-gnueabihf/"
        "\nRun: just pi::fetch-png-sysroot\n"
    )
endif()

# ─── Библиотека рендерера ─────────────────────────────────────────────────────

add_library(dispmanx_renderer STATIC
    renderer_impl.c
    layers/image.c
    layers/imageLayer.c
    layers/loadpng.c
)

# Публичный include: src_indicator/renderer/renderer.h
target_include_directories(dispmanx_renderer PUBLIC
    ${CMAKE_SOURCE_DIR}/src_indicator
)

# Приватные includes: Duncan headers + DispmanX + libpng/zlib из Pi sysroot
target_include_directories(dispmanx_renderer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/layers
)
target_include_directories(dispmanx_renderer SYSTEM PRIVATE
    /opt/vc/include
    /opt/vc/include/interface/vcos/pthreads
    /opt/vc/include/interface/vmcs_host/linux
    ${PI_USR_SYSROOT}/include   # png.h, zlib.h (ARM32)
)

# Линковка:
#   bcm_host  — через -L/opt/vc/lib (уже задан в Toolchain), передаём явно для ясности
#   png16, z  — строки "-L..." и "-lpng16" обходят CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY
#               и передаются линкеру напрямую
target_link_libraries(dispmanx_renderer PUBLIC
    "-L/opt/vc/lib" bcm_host
    "-L${PI_USR_SYSROOT}/lib/arm-linux-gnueabihf" "-lpng16" "-lz"
)

# Предупреждения: применяем только к нашему renderer_impl.c
apply_warnings(dispmanx_renderer)

# Vendored Duncan-код: подавить все предупреждения
set_source_files_properties(
    layers/image.c
    layers/imageLayer.c
    layers/loadpng.c
    PROPERTIES COMPILE_OPTIONS "-w"
)