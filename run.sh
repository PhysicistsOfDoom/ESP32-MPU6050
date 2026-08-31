#!/usr/bin/env bash
#
# run.sh — build + flash the ESP32, then launch the Python receiver.
#
# Why not just "idf.py build flash monitor" followed by the python script?
# Because idf.py monitor and the python script both need exclusive access
# to the same serial port — only one can hold it open at a time. This
# script flashes WITHOUT monitor, waits for the board to reboot, then
# hands the port over to Python.
#
# Usage:
#   ./run.sh              # build, flash, then run frame_parser.py
#   ./run.sh --no-build    # skip build+flash, just run the receiver

set -e  # stop immediately if any command fails

PORT="/dev/ttyUSB0"
RECEIVER="frame_parser.py"
DO_BUILD=true

for arg in "$@"; do
    case $arg in
        --no-build)
            DO_BUILD=false
            ;;
        --port=*)
            PORT="${arg#*=}"
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Usage: ./run.sh [--no-build] [--port=/dev/ttyUSB0]"
            exit 1
            ;;
    esac
done

if [ "$DO_BUILD" = true ]; then
    if ! command -v idf.py &> /dev/null; then
        echo "==> idf.py not found on PATH, sourcing ESP-IDF export.sh..."
        IDF_PATH="${IDF_PATH:-$HOME/.espressif/v6.1/esp-idf}"
        source "$IDF_PATH/export.sh"
    fi

    echo "==> Building and flashing firmware..."
    idf.py build flash
    echo "==> Flash complete. Waiting for board to reboot..."
    sleep 2
else
    echo "==> Skipping build/flash (--no-build passed)."
fi

if [ ! -e "$PORT" ]; then
    echo "ERROR: $PORT not found. Is the ESP32 plugged in?"
    echo "Try: ls /dev/ttyUSB*"
    exit 1
fi

echo "==> Starting $RECEIVER on $PORT ..."
python3 "$RECEIVER" "$PORT"
