# =============================================================================
# build-env/Dockerfile
# Образ для кросс-компиляции под Raspberry Pi Zero 2W (ARMv7 / armhf)
# и host unit-тестов.
#
# Содержит:
#   - arm-linux-gnueabihf-gcc  ← кросс-компилятор
#   - clang / clang-tidy / clang-format  ← host тесты + анализ
#   - cmake / ninja
#   - Pi sysroot (/opt/vc)  ← DispmanX, bcm_host (копируется с Pi)
#   - libpng-dev (host)
#
# Сборка: just pi::build-image
# =============================================================================

FROM debian:bookworm-slim

LABEL maintainer="lift-indicator"
LABEL description="Cross-compile + host test environment for Raspberry Pi Zero 2W"

# ─── Системные пакеты ────────────────────────────────────────────────────────

RUN apt-get update && apt-get install -y --no-install-recommends \
    # Кросс-компилятор ARM
    gcc-arm-linux-gnueabihf \
    g++-arm-linux-gnueabihf \
    binutils-arm-linux-gnueabihf \
    # Host компилятор и инструменты
    clang \
    clang-tidy \
    clang-format \
    # Сборка
    cmake \
    ninja-build \
    make \
    # Зависимости host сборки
    libpng-dev \
    # Утилиты
    git \
    rsync \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# ─── just (task runner) ──────────────────────────────────────────────────────

ARG JUST_VERSION=1.36.0
RUN curl -LsSf "https://github.com/casey/just/releases/download/${JUST_VERSION}/just-${JUST_VERSION}-x86_64-unknown-linux-musl.tar.gz" \
    | tar -xz -C /usr/local/bin just

# ─── Pi sysroot (DispmanX, bcm_host, libbcm_host.so) ────────────────────────
# Копируется с реального Pi командой: just pi::fetch-sysroot
# Затем пересобрать образ: just pi::build-image

COPY pi-sysroot/vc /opt/vc

# Сделать /opt/vc lib доступными линкеру при кросс-компиляции
RUN echo "/opt/vc/lib" > /etc/ld.so.conf.d/vc.conf && ldconfig || true

# ─── Рабочая директория ──────────────────────────────────────────────────────

WORKDIR /project

# ─── Проверка ────────────────────────────────────────────────────────────────

RUN echo "=== Toolchain versions ===" && \
    arm-linux-gnueabihf-gcc --version && \
    clang --version && \
    cmake --version && \
    just --version
