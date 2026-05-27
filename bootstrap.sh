#!/usr/bin/env bash
# =============================================================================
# bootstrap.sh — уровень 0
# Устанавливает uv (если нет/устарел), затем just через uv tool,
# и передаёт управление just pi::bootstrap.
#
# Поддержка: macOS (bash), Linux (bash), Windows (Git Bash)
# Требования: bash >= 4, curl (macOS/Linux) или PowerShell (Windows)
# Запуск: ./bootstrap.sh
# =============================================================================
set -euo pipefail

JUST_VERSION="1.36.0"
UV_VERSION="0.4.0"
LINUX_INSTALL_DIR="${HOME}/.local/bin"

BOLD="\033[1m"
RED="\033[0;31m"
YELLOW="\033[1;33m"
GREEN="\033[0;32m"
RESET="\033[0m"

info()    { echo -e " ${BOLD}${*}${RESET}"; }
success() { echo -e " ${GREEN}✅ ${*}${RESET}"; }
warn()    { echo -e " ${YELLOW}⚠️  ${*}${RESET}"; }
error()   { echo -e " ${RED}❌ ${*}${RESET}"; }

echo ""
echo -e "${BOLD}=== Lift Indicator — Bootstrap ===${RESET}"
echo ""

# ─── Определить платформу ────────────────────────────────────────────────────

_uname="$(uname -s)"
case "${_uname}" in
    Linux*)               PLATFORM="linux"   ;;
    Darwin*)              PLATFORM="macos"   ;;
    MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
    *)
        error "Unsupported platform: ${_uname}"
        exit 1
        ;;
esac

info "Platform: ${PLATFORM}"
echo ""

# ─── Semver без sort -V (не работает в Git Bash) ─────────────────────────────

semver_ge() {
    local a="$1" b="$2"; local a1 a2 a3 b1 b2 b3
    IFS='.' read -r a1 a2 a3 <<< "${a}"; IFS='.' read -r b1 b2 b3 <<< "${b}"
    a1="${a1//[^0-9]/}"; a2="${a2//[^0-9]/}"; a3="${a3//[^0-9]/}"
    b1="${b1//[^0-9]/}"; b2="${b2//[^0-9]/}"; b3="${b3//[^0-9]/}"
    [[ "${a1:-0}" -gt "${b1:-0}" ]] && return 0; [[ "${a1:-0}" -lt "${b1:-0}" ]] && return 1
    [[ "${a2:-0}" -gt "${b2:-0}" ]] && return 0; [[ "${a2:-0}" -lt "${b2:-0}" ]] && return 1
    [[ "${a3:-0}" -ge "${b3:-0}" ]] && return 0; return 1
}

# ─── Установка uv ────────────────────────────────────────────────────────────

install_uv_linux() {
    info "Installing uv → ${LINUX_INSTALL_DIR}"
    mkdir -p "${LINUX_INSTALL_DIR}"
    curl -LsSf https://astral.sh/uv/install.sh | env UV_INSTALL_DIR="${LINUX_INSTALL_DIR}" sh
    if ! echo "${PATH}" | grep -q "${LINUX_INSTALL_DIR}"; then
        warn "${LINUX_INSTALL_DIR} not in PATH — adding for this session"
        warn "Add to ~/.bashrc: export PATH=\"${LINUX_INSTALL_DIR}:\$PATH\""
        export PATH="${LINUX_INSTALL_DIR}:${PATH}"
    fi
}

install_uv_macos() {
    if command -v brew &>/dev/null; then
        info "Installing uv via Homebrew..."
        brew install uv
    else
        install_uv_linux
    fi
}

install_uv_windows() {
    if command -v powershell.exe &>/dev/null; then
        info "Installing uv via PowerShell..."
        powershell.exe -ExecutionPolicy ByPass \
            -Command "irm https://astral.sh/uv/install.ps1 | iex" || {
            error "PowerShell install failed. Run manually in PowerShell:"
            echo '    irm https://astral.sh/uv/install.ps1 | iex'
            echo "  Then restart Git Bash and re-run: ./bootstrap.sh"
            exit 1
        }
        UV_WIN_DIR="${USERPROFILE}/.local/bin"
        [[ -d "${UV_WIN_DIR}" ]] && export PATH="${UV_WIN_DIR}:${PATH}"
    else
        error "powershell.exe not found. Install uv manually."
        exit 1
    fi
}

# ─── Проверить / установить uv ───────────────────────────────────────────────

info "--- Checking uv ---"
UV_OK=false

if command -v uv &>/dev/null; then
    UV_CURRENT="$(uv --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
    if semver_ge "${UV_CURRENT}" "${UV_VERSION}"; then
        success "uv ${UV_CURRENT} (>= ${UV_VERSION})"
        UV_OK=true
    else
        warn "uv ${UV_CURRENT} outdated (need >= ${UV_VERSION}), reinstalling..."
    fi
fi

if [[ "${UV_OK}" == "false" ]]; then
    case "${PLATFORM}" in
        linux)   install_uv_linux   ;;
        macos)   install_uv_macos   ;;
        windows) install_uv_windows ;;
    esac
    UV_CURRENT="$(uv --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
    success "uv ${UV_CURRENT} installed"
fi

# ─── Проверить / установить just ─────────────────────────────────────────────

echo ""
info "--- Checking just ---"
JUST_OK=false

if command -v just &>/dev/null; then
    JUST_CURRENT="$(just --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
    if semver_ge "${JUST_CURRENT}" "${JUST_VERSION}"; then
        success "just ${JUST_CURRENT} (>= ${JUST_VERSION})"
        JUST_OK=true
    else
        warn "just ${JUST_CURRENT} outdated (need >= ${JUST_VERSION}), reinstalling..."
    fi
fi

if [[ "${JUST_OK}" == "false" ]]; then
    info "Installing just ${JUST_VERSION} via uv tool..."
    uv tool install "rust-just==${JUST_VERSION}"
    UV_TOOL_BIN="$(uv tool dir --bin 2>/dev/null || echo "${LINUX_INSTALL_DIR}")"
    if ! echo "${PATH}" | grep -q "${UV_TOOL_BIN}"; then
        warn "${UV_TOOL_BIN} not in PATH — adding for this session"
        warn "Add to your shell rc: export PATH=\"${UV_TOOL_BIN}:\$PATH\""
        export PATH="${UV_TOOL_BIN}:${PATH}"
    fi
    JUST_CURRENT="$(just --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
    success "just ${JUST_CURRENT} installed"
fi

# ─── Передать управление just pi::bootstrap ──────────────────────────────────

echo ""
info "Delegating to: just pi::bootstrap"
echo ""
exec just pi::bootstrap "$@"
