#!/bin/bash
# Add Telegram credentials to an already-provisioned device without losing existing NVS data.
# Usage: ./scripts/add-telegram.sh [--port <port>] --tg-token <token> --tg-chat-id <id>

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
PORT=""
TG_TOKEN=""
TG_CHAT_ID=""

usage() {
    cat << EOF
Usage: $0 [options]

Options:
  --port <serial-port>   Serial port (auto-detect if omitted)
  --tg-token <token>     Telegram bot token (required)
  --tg-chat-id <id>      Telegram chat ID (required)
  -h, --help             Show help
EOF
}

source_idf_env() {
    local candidates=(
        "$HOME/esp/esp-idf/export.sh"
        "$HOME/esp/v5.4/esp-idf/export.sh"
    )
    [ -n "${IDF_PATH:-}" ] && candidates+=("$IDF_PATH/export.sh")

    for script in "${candidates[@]}"; do
        if [ -f "$script" ] && source "$script" > /dev/null 2>&1; then
            [ -z "${IDF_PATH:-}" ] && IDF_PATH="$(cd "$(dirname "$script")" && pwd)"
            return 0
        fi
    done
    echo "Error: ESP-IDF not found"; return 1
}

detect_serial_port() {
    local ports=()
    shopt -s nullglob
    if [ "$(uname -s)" = "Darwin" ]; then
        ports+=(/dev/cu.usbserial-* /dev/cu.usbmodem*)
    else
        ports+=(/dev/ttyUSB* /dev/ttyACM*)
    fi
    shopt -u nullglob

    if [ "${#ports[@]}" -eq 0 ]; then
        echo "Error: no serial port detected. Use --port." >&2; return 1
    fi
    if [ "${#ports[@]}" -gt 1 ]; then
        echo "Multiple ports detected, using first: ${ports[0]}" >&2
    fi
    echo "${ports[0]}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --port)     shift; PORT="$1" ;;
        --port=*)   PORT="${1#*=}" ;;
        --tg-token) shift; TG_TOKEN="$1" ;;
        --tg-token=*) TG_TOKEN="${1#*=}" ;;
        --tg-chat-id) shift; TG_CHAT_ID="$1" ;;
        --tg-chat-id=*) TG_CHAT_ID="${1#*=}" ;;
        -h|--help)  usage; exit 0 ;;
        *)          echo "Unknown option: $1"; usage; exit 1 ;;
    esac
    shift
done

if [ -z "$TG_TOKEN" ] || [ -z "$TG_CHAT_ID" ]; then
    echo "Error: --tg-token and --tg-chat-id are required"
    usage
    exit 1
fi

source_idf_env || exit 1

if [ -z "$PORT" ]; then
    PORT="$(detect_serial_port)" || exit 1
fi

NVS_TOOL="$IDF_PATH/components/nvs_flash/nvs_partition_tool/nvs_tool.py"
NVS_GEN="$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
PARTTOOL="$IDF_PATH/components/partition_table/parttool.py"

for tool in "$NVS_TOOL" "$NVS_GEN" "$PARTTOOL"; do
    [ -f "$tool" ] || { echo "Error: $(basename "$tool") not found at $tool"; exit 1; }
done

tmpdir="$(mktemp -d 2>/dev/null || mktemp -d -t zclaw-tg)"
trap 'rm -rf "$tmpdir"' EXIT

echo "Reading current NVS partition from $PORT..."
python "$PARTTOOL" --port "$PORT" read_partition --partition-name nvs --output "$tmpdir/nvs_current.bin"

echo "Parsing existing NVS keys..."
existing="$(python "$NVS_TOOL" "$tmpdir/nvs_current.bin" -d minimal -f text 2>/dev/null || true)"

csv_file="$tmpdir/nvs.csv"
echo "key,type,encoding,value" > "$csv_file"
echo "zclaw,namespace,," >> "$csv_file"

# Preserve existing keys from the zclaw namespace
while IFS= read -r line; do
    [ -z "$line" ] && continue
    # Lines look like: zclaw:key = value
    if [[ "$line" =~ ^zclaw:([a-z_]+)\ =\ (.*)$ ]]; then
        key="${BASH_REMATCH[1]}"
        value="${BASH_REMATCH[2]}"
        # Skip telegram keys (we'll write new ones) and internal counters
        case "$key" in
            tg_token|tg_chat_id) continue ;;
        esac
        # Escape value for CSV
        value="${value//\"/\"\"}"
        echo "$key,data,string,\"$value\"" >> "$csv_file"
    fi
done <<< "$existing"

# Add telegram credentials
echo "tg_token,data,string,\"$TG_TOKEN\"" >> "$csv_file"
echo "tg_chat_id,data,string,\"$TG_CHAT_ID\"" >> "$csv_file"

echo "Generating new NVS image..."
python "$NVS_GEN" generate "$csv_file" "$tmpdir/nvs_new.bin" 0x4000

echo "Writing updated NVS to $PORT..."
python "$PARTTOOL" --port "$PORT" write_partition --partition-name nvs --input "$tmpdir/nvs_new.bin"

echo ""
echo "Telegram configured successfully!"
echo "  Bot token: ${TG_TOKEN:0:10}..."
echo "  Chat ID:   $TG_CHAT_ID"
echo ""
echo "Reset the board to apply changes."
