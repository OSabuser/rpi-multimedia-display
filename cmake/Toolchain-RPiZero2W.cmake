# cmake/Toolchain-RPiZero2W.cmake
# Кросс-компиляция под Raspberry Pi Zero 2W (ARMv7 hard-float, Buster)
#
# P-08 v2: CMAKE_SYSROOT убран.
# Кросс-компилятор из Debian Buster уже содержит Buster ARM libc (2.28)
# в своём встроенном sysroot (/usr/arm-linux-gnueabihf).
# Бинари автоматически будут требовать glibc >= 2.28.
#
# /opt/vc — DispmanX и bcm_host (копируется с Pi, монтируется в образ).

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CROSS_TRIPLE "arm-linux-gnueabihf")

set(CMAKE_C_COMPILER   "${CROSS_TRIPLE}-gcc")
set(CMAKE_CXX_COMPILER "${CROSS_TRIPLE}-g++")
set(CMAKE_STRIP        "${CROSS_TRIPLE}-strip")

# ─── Pi Zero 2W: ARM Cortex-A53, armv8 в 32-bit режиме ──────────────────────

set(CMAKE_C_FLAGS_INIT
    "-march=armv8-a -mtune=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard")

# ─── DispmanX и bcm_host ─────────────────────────────────────────────────────
# /opt/vc скопирован с Pi командой: just pi::fetch-sysroot
# Монтируется в Docker-образ как /opt/vc.

set(VC_DIR "/opt/vc")

include_directories(SYSTEM
    "${VC_DIR}/include"
    "${VC_DIR}/include/interface/vcos/pthreads"
    "${VC_DIR}/include/interface/vmcs_host/linux"
)

link_directories("${VC_DIR}/lib")

# ─── Поиск библиотек ─────────────────────────────────────────────────────────

set(CMAKE_FIND_ROOT_PATH "${VC_DIR}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)