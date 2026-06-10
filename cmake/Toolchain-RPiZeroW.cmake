# cmake/Toolchain-RPiZeroW.cmake
#
# Кросс-компиляция для Pi Zero W (ARM1176JZF-S, ARMv6ZK, VFPv2).
#
# Компилятор: zig cc через wrapper /usr/local/bin/arm-zig-cc
# Zig компилирует crt*.o точно под arm1176jzf_s — в отличие от
# Debian arm-linux-gnueabihf, чьи crt-файлы захардкожены под ARMv7.

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# ─── Компилятор: zig cc wrapper ──────────────────────────────────────────────
# Wrapper создаётся в Dockerfile:
#   echo '#!/bin/sh\nexec zig cc -target arm-linux-gnueabihf -mcpu=arm1176jzf_s "$@"' \
#        > /usr/local/bin/arm-zig-cc

set(CMAKE_C_COMPILER /usr/local/bin/arm-zig-cc)

# Zig cc идентифицирует себя как clang.
# CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY: cmake тестирует компилятор
# только через компиляцию объектника, без линковки — zig cc справляется.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ─── Флаги ───────────────────────────────────────────────────────────────────
# -marm:           ARM32-mode (не Thumb). Zig может выбрать Thumb по умолчанию.
# -mfloat-abi=hard: совместимость с Pi OS armhf ABI (hard-float).
# Arch/cpu уже задан в wrapper через -mcpu=arm1176jzf_s.

set(CMAKE_C_FLAGS_INIT "-marm -mfloat-abi=hard")

# ─── DispmanX и bcm_host ─────────────────────────────────────────────────────

set(VC_DIR "/opt/vc")

include_directories(SYSTEM
    "${VC_DIR}/include"
    "${VC_DIR}/include/interface/vcos/pthreads"
    "${VC_DIR}/include/interface/vmcs_host/linux"
)

link_directories("${VC_DIR}/lib")

set(CMAKE_FIND_ROOT_PATH "${VC_DIR}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)