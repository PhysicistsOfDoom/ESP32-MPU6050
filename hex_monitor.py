#!/usr/bin/env python3
"""
Simple UART hex monitor.
Reads raw bytes from a serial port and prints them as hex values,
instead of trying to interpret them as ASCII text.
"""

import serial
import sys

PORT = "/dev/ttyUSB0"   # change if your ESP32 shows up on a different port
BAUD = 115200            # must match your ESP32 code's baud rate

def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
    except serial.SerialException as e:
        print(f"Could not open {PORT}: {e}")
        sys.exit(1)

    print(f"Listening on {PORT} at {BAUD} baud. Press Ctrl+C to stop.\n")

    try:
        while True:
            data = ser.read(64)  # read up to 64 bytes at a time
            if data:
                hex_str = " ".join(f"0x{b:02X}" for b in data)
                print(hex_str)
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        ser.close()

if __name__ == "__main__":
    main()
