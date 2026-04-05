#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="${0:a:h}"
cd "$SCRIPT_DIR"

# Use the Nordic toolchain (has correct Python env with pykwalify etc.)
export ZEPHYR_BASE="/opt/nordic/ncs/v3.2.4/zephyr"
export PATH="/opt/nordic/ncs/toolchains/185bb0e3b6/bin:$PATH"

# Read version from source
VERSION=$(sed -n 's/^#define APP_VERSION "\(.*\)"/\1/p' src/version.h)
APP_NAME=$(sed -n 's/^#define APP_BUILD_NAME "\(.*\)"/\1/p' src/version.h)

if [[ -z "$VERSION" || -z "$APP_NAME" ]]; then
    echo "Error: Could not parse version or app name from src/version.h"
    exit 1
fi

echo "Building ${APP_NAME} v${VERSION} for all targets..."

# Define targets: build_dir, display name, west args
typeset -a BUILDS
BUILDS=(
    "build_bl54l15_dvk|bl54l15_dvk|--board bl54l15_dvk/nrf54l15/cpuapp -- -DCONFIG_DEBUG_THREAD_INFO=y -DDTC_OVERLAY_FILE=boards/bl54l15_dvk_nrf54l15_cpuapp.overlay"
    "build_bl54l15u_dvk|bl54l15u_dvk|--board bl54l15u_dvk/nrf54l15/cpuapp -- -DDTC_OVERLAY_FILE=boards/bl54l15u_dvk_nrf54l15_cpuapp.overlay -DDEBUG_THREAD_INFO=On -DCONFIG_DEBUG_THREAD_INFO=y -Dcs_at_command_DEBUG_THREAD_INFO=On"
    "build_bl54l15u_dvk_dual-antenna|bl54l15u_dvk_dual-antenna|--board bl54l15u_dvk/nrf54l15/cpuapp -- -DEXTRA_CONF_FILE=boards/bl54l15u_dvk_dual-antenna_cpuapp.conf -DEXTRA_DTC_OVERLAY_FILE=boards/bl54l15u_dvk_dual-antenna_cpuapp.overlay -DDEBUG_THREAD_INFO=On -DCONFIG_DEBUG_THREAD_INFO=y -Dcs_at_command_DEBUG_THREAD_INFO=On"
)

FAIL=0

for ENTRY in "${BUILDS[@]}"; do
    BUILD_DIR="${ENTRY%%|*}"
    local REST="${ENTRY#*|}"
    TARGET="${REST%%|*}"
    ARGS="${REST#*|}"
    echo ""
    echo "========================================="
    echo "Building: ${TARGET}"
    echo "Build dir: ${BUILD_DIR}"
    echo "========================================="

    if west build --build-dir "$BUILD_DIR" . --pristine ${=ARGS}; then
        HEX_SRC="${BUILD_DIR}/merged.hex"
        if [[ -f "$HEX_SRC" ]]; then
            HEX_DST="${APP_NAME}_v${VERSION}_${TARGET}.hex"
            cp "$HEX_SRC" "$HEX_DST"
            echo "Copied: ${HEX_DST}"
        else
            echo "Warning: ${HEX_SRC} not found"
            FAIL=1
        fi
    else
        echo "FAILED: ${TARGET}"
        FAIL=1
    fi
done

echo ""
if [[ $FAIL -eq 0 ]]; then
    echo "All builds succeeded. Hex files:"
    ls -1 ${APP_NAME}_v${VERSION}_*.hex
else
    echo "Some builds failed!"
    exit 1
fi
