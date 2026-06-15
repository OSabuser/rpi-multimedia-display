# syntax=docker/dockerfile:1
# =============================================================================
# build-env/Dockerfile
#
# Stage 1 (zig-stage): скачиваем zig — кэшируется при изменении ZIG_VERSION.
# Stage 2 (dev):       Ubuntu 22.04 + LLVM 17 + dev-инструменты.
#
# Почему Ubuntu 22.04, а не Buster:
#   - zig cc полностью self-contained: crt*.o, libc компилируются под
#     целевой arm1176jzf_s независимо от хостового дистрибутива.
#   - Ubuntu 22.04 (Jammy) поддерживается до 2027, актуальные пакеты.
#   - LLVM официально поддерживает Jammy → clangd-17, clang-tidy-17
#     устанавливаются без танцев с архивами.
# =============================================================================

# ─── Stage 1: zig ────────────────────────────────────────────────────────────

FROM ubuntu:22.04 AS zig-stage

ARG ZIG_VERSION=0.13.0

RUN apt-get update && apt-get install -y --no-install-recommends \
    curl ca-certificates xz-utils \
    && rm -rf /var/lib/apt/lists/*

RUN set -eux; \
    ARCH="$(uname -m)"; \
    case "$ARCH" in \
        x86_64)  ZIG_ARCH="x86_64"  ;; \
        aarch64) ZIG_ARCH="aarch64" ;; \
        *) echo "Unsupported arch: $ARCH" && exit 1 ;; \
    esac; \
    mkdir -p /opt/zig && \
    curl -fsSL --retry 5 --retry-delay 3 \
        "https://ziglang.org/download/${ZIG_VERSION}/zig-linux-${ZIG_ARCH}-${ZIG_VERSION}.tar.xz" \
        -o /tmp/zig.tar.xz && \
    tar -xJ -C /opt/zig --strip-components=1 -f /tmp/zig.tar.xz && \
    rm /tmp/zig.tar.xz

# ─── Stage 2: dev ────────────────────────────────────────────────────────────

FROM ubuntu:22.04 AS dev

LABEL maintainer="lift-indicator"

ENV DEBIAN_FRONTEND=noninteractive

# ─── LLVM 17: clangd, clang-tidy, clang-format ───────────────────────────────
# Официальный LLVM apt repo для Ubuntu Jammy.
# clangd-17 необходим для поддержки `If:` секций в .clangd (multi-profile).

RUN apt-get update && apt-get install -y --no-install-recommends \
        gnupg ca-certificates curl wget && \
    curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key \
    | gpg --dearmor -o /usr/share/keyrings/llvm.gpg && \
    echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/jammy/ llvm-toolchain-jammy-17 main" \
    > /etc/apt/sources.list.d/llvm.list && \
    rm -rf /var/lib/apt/lists/*

# ─── Основные пакеты ─────────────────────────────────────────────────────────

RUN apt-get update && apt-get install -y --no-install-recommends \
    # LLVM 17 инструменты
    clang-17 \
    clangd-17 \
    imagemagick \
    libclang-rt-17-dev \
    clang-format-17 \
    clang-tidy-17 \
    # Remote debug
    gdb-multiarch \
    # Для хост-тестов
    libpng-dev \
    # SSH для just pi::* и GDB tunnel
    openssh-client \
    # Сборка
    make \
    ninja-build \
    # Утилиты
    git \
    rsync \
    file \
    && rm -rf /var/lib/apt/lists/*

# ─── Алиасы: clang-17 → clang ────────────────────────────────────────────────

RUN update-alternatives --install /usr/bin/clangd       clangd       /usr/bin/clangd-17       100 && \
    update-alternatives --install /usr/bin/clang        clang        /usr/bin/clang-17        100 && \
    update-alternatives --install /usr/bin/clang-tidy   clang-tidy   /usr/bin/clang-tidy-17   100 && \
    update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-17 100

# ─── cmake ───────────────────────────────────────────────────────────────────

ARG CMAKE_VERSION=3.28.3
RUN set -eux; \
    ARCH="$(uname -m)"; \
    case "$ARCH" in \
        x86_64)  CMAKE_ARCH="linux-x86_64"  ;; \
        aarch64) CMAKE_ARCH="linux-aarch64" ;; \
        *) echo "Unsupported arch: $ARCH" && exit 1 ;; \
    esac; \
    curl -fsSL \
        "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-${CMAKE_ARCH}.tar.gz" \
    | tar -xz -C /usr/local --strip-components=1

# ─── just ────────────────────────────────────────────────────────────────────

ARG JUST_VERSION=1.36.0
RUN set -eux; \
    ARCH="$(uname -m)"; \
    case "$ARCH" in \
        x86_64)  JUST_ARCH="x86_64"  ;; \
        aarch64) JUST_ARCH="aarch64" ;; \
        *) echo "Unsupported arch: $ARCH" && exit 1 ;; \
    esac; \
    curl -fsSL \
        "https://github.com/casey/just/releases/download/${JUST_VERSION}/just-${JUST_VERSION}-${JUST_ARCH}-unknown-linux-musl.tar.gz" \
    | tar -xz -C /usr/local/bin just

# ─── zig (из Stage 1) ────────────────────────────────────────────────────────

COPY --from=zig-stage /opt/zig /opt/zig
RUN ln -s /opt/zig/zig /usr/local/bin/zig

# ─── Wrapper: arm-zig-cc ─────────────────────────────────────────────────────

RUN printf '#!/bin/sh\nexec zig cc -target arm-linux-gnueabihf -mcpu=arm1176jzf_s "$@"\n' \
    > /usr/local/bin/arm-zig-cc && chmod +x /usr/local/bin/arm-zig-cc

# ─── Wrapper: arm-zig-cc-a53 (Pi Zero 2W, Cortex-A53) ───────────────────────

RUN printf '#!/bin/sh\nexec zig cc -target arm-linux-gnueabihf -mcpu=cortex_a53 "$@"\n' \
    > /usr/local/bin/arm-zig-cc-a53 && chmod +x /usr/local/bin/arm-zig-cc-a53

# ─── Pi sysroot: /opt/vc (DispmanX, bcm_host) ────────────────────────────────

COPY pi-sysroot/opt/vc /opt/vc

RUN echo "/opt/vc/lib" > /etc/ld.so.conf.d/vc.conf && ldconfig || true

# ─── Рабочая директория ──────────────────────────────────────────────────────

WORKDIR /project

# ─── Проверка ────────────────────────────────────────────────────────────────

RUN echo "=== zig ===" && zig version && \
    echo "=== arm-zig-cc ===" && arm-zig-cc --version 2>&1 | head -1 && \
    echo "=== arm-zig-cc-a53 ===" && arm-zig-cc-a53 --version 2>&1 | head -1 && \
    echo "=== clangd ===" && clangd --version && \
    echo "=== clang-format ===" && clang-format --version && \
    echo "=== gdb-multiarch ===" && gdb-multiarch --version | head -1 && \
    echo "=== cmake ===" && cmake --version | head -1 && \
    echo "=== just ===" && just --version
