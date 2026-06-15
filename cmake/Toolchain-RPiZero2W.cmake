# cmake/Toolchain-RPiZero2W.cmake
#
# Кросс-компиляция для Pi Zero 2W (Cortex-A53, ARMv8 32-bit, Buster armhf).
#
# Компилятор: zig cc через wrapper /usr/local/bin/arm-zig-cc-a53
# -mcpu=cortex_a53 → zig генерирует crt*.o точно под A53.
# Thumb-2 разрешён (в отличие от ARMv6, флаг -marm не нужен).

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# ─── Компилятор: zig cc wrapper ──────────────────────────────────────────────

set(CMAKE_C_COMPILER /usr/local/bin/arm-zig-cc-a53)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ─── Флаги ───────────────────────────────────────────────────────────────────
# -mfloat-abi=hard: armhf ABI.
# -marm не нужен: Cortex-A53 полноценно исполняет Thumb-2.

set(CMAKE_C_FLAGS_INIT "-mfloat-abi=hard")

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