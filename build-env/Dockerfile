# =============================================================================
# build-env/Dockerfile
# =============================================================================

FROM debian:buster-slim

LABEL maintainer="lift-indicator"

# ─── Buster EOL → архивные репозитории ───────────────────────────────────────

RUN echo "deb http://archive.debian.org/debian buster main contrib non-free" > /etc/apt/sources.list && \
    echo "deb http://archive.debian.org/debian-security buster/updates main contrib non-free" >> /etc/apt/sources.list && \
    echo 'Acquire::Check-Valid-Until "false";' > /etc/apt/apt.conf.d/99no-check-valid

# ─── Системные пакеты ────────────────────────────────────────────────────────

RUN apt-get update && apt-get install -y --no-install-recommends \
    clang \
    clang-tidy \
    clang-format \
    make \
    ninja-build \
    libpng-dev \
    git \
    rsync \
    curl \
    ca-certificates \
    xz-utils \
    file \
    && rm -rf /var/lib/apt/lists/*

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

# ─── zig ─────────────────────────────────────────────────────────────────────
# Zig-тарбол: zig-linux-ARCH-VER/{zig,lib/,doc/,...}
# Распаковываем в /opt/zig/, бинарь симлинкуем в PATH.
# Zig ищет свою lib/ рядом с собой → держим вместе в /opt/zig/.

ARG ZIG_VERSION=0.13.0
RUN set -eux; \
    ARCH="$(uname -m)"; \
    case "$ARCH" in \
        x86_64)  ZIG_ARCH="x86_64"  ;; \
        aarch64) ZIG_ARCH="aarch64" ;; \
        *) echo "Unsupported arch: $ARCH" && exit 1 ;; \
    esac; \
    mkdir -p /opt/zig && \
    curl -fsSL \
        "https://ziglang.org/download/${ZIG_VERSION}/zig-linux-${ZIG_ARCH}-${ZIG_VERSION}.tar.xz" \
    | tar -xJ -C /opt/zig --strip-components=1 && \
    ln -s /opt/zig/zig /usr/local/bin/zig

# ─── Wrapper: arm-zig-cc ─────────────────────────────────────────────────────
# Pi Zero W: ARM1176JZF-S = arm1176jzf_s в терминологии zig/llvm.

RUN printf '#!/bin/sh\nexec zig cc -target arm-linux-gnueabihf -mcpu=arm1176jzf_s "$@"\n' \
    > /usr/local/bin/arm-zig-cc && chmod +x /usr/local/bin/arm-zig-cc

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

# ─── Pi sysroot: /opt/vc ─────────────────────────────────────────────────────

COPY pi-sysroot/opt/vc /opt/vc

RUN echo "/opt/vc/lib" > /etc/ld.so.conf.d/vc.conf && ldconfig || true

# ─── Рабочая директория ──────────────────────────────────────────────────────

WORKDIR /project

# ─── Проверка ────────────────────────────────────────────────────────────────

RUN echo "=== zig ===" && zig version && \
    echo "=== arm-zig-cc ===" && arm-zig-cc --version 2>&1 | head -1 && \
    echo "=== cmake ===" && cmake --version | head -1 && \
    echo "=== just ===" && just --version