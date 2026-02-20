#!/bin/bash
# Build zclaw firmware

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

source_idf_env() {
    local candidates=(
        "$HOME/esp/esp-idf/export.sh"
        "$HOME/esp/v5.4/esp-idf/export.sh"
    )
    if [ -n "${IDF_PATH:-}" ]; then
        candidates+=("$IDF_PATH/export.sh")
    fi

    local script
    local found=0
    for script in "${candidates[@]}"; do
        [ -f "$script" ] || continue
        found=1
        if source "$script" > /dev/null 2>&1; then
            return 0
        fi
    done

    if [ "$found" -eq 1 ]; then
        echo "Error: ESP-IDF found but failed to activate."
        echo "Run:"
        echo "  cd ~/esp/esp-idf && ./install.sh esp32c3,esp32s3"
    else
        echo "Error: ESP-IDF not found. Install it first:"
        echo "  mkdir -p ~/esp && cd ~/esp"
        echo "  git clone -b v5.4 --recursive https://github.com/espressif/esp-idf.git"
        echo "  cd esp-idf && ./install.sh esp32c3,esp32s3"
    fi
    return 1
}

source_idf_env || exit 1

echo "Building zclaw..."
idf.py build

echo ""
echo "Build complete! To flash: ./scripts/flash.sh"
