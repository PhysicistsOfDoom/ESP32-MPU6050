#!/usr/bin/env python3
"""
Parses the UART frame protocol from the ESP32 MPU6050 project.

Frame layout (24 bytes total):
  [0]     0xAA        sync byte 1
  [1]     0x55        sync byte 2
  [2]     LEN         payload length (always 18 for this project)
  [3]     SEQ         sequence number, wraps at 255
  [4:8]   ts_us        4-byte timestamp, little-endian, microseconds since boot
  [8:22]  7x int16     ax, ay, az, temp, gx, gy, gz -- each big-endian
  [22:24] CRC-16       big-endian, CRC-16/CCITT-FALSE over bytes [2:22] (LEN+SEQ+payload)
"""

import serial
import sys
import struct

PORT = "/dev/ttyUSB0" # Change this of course if using a different device mount.
BAUD = 115200

SYNC1 = 0xAA
SYNC2 = 0x55
FRAME_LEN = 24          # total frame size
PAYLOAD_LEN = 18        # LEN field value (SEQ is separate, not counted in payload per the C++ code... see note below)
CRC_COVERAGE_LEN = 20   # LEN+SEQ+payload = 1+1+18 = 20 bytes covered by CRC


def crc16_ccitt(data: bytes) -> int:
    """Matches the ESP32's crc16_ccitt() exactly: poly 0x1021, init 0xFFFF."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def try_parse_frame(buf: bytes):
    """
    Attempt to parse one frame starting at buf[0].
    Returns (frame_dict, bytes_consumed) on success, or (None, 1) on failure
    -- the '1' means 'advance one byte and try again', our resync strategy.
    """
    if len(buf) < FRAME_LEN:
        return None, 0  # not enough data yet, wait for more

    if buf[0] != SYNC1 or buf[1] != SYNC2:
        return None, 1  # not a sync point, advance one byte

    length = buf[2]
    if length != PAYLOAD_LEN:
        return None, 1  # LEN doesn't match what we expect -- false sync, resync

    seq = buf[3]
    payload = buf[4:22]
    crc_received = (buf[22] << 8) | buf[23]

    crc_computed = crc16_ccitt(buf[2:22])  # LEN + SEQ + payload
    if crc_computed != crc_received:
        return None, 1  # CRC failed -- corrupt or false-locked frame, resync

    ts_us = struct.unpack_from("<I", payload, 0)[0]           # little-endian uint32
    ax, ay, az, temp, gx, gy, gz = struct.unpack_from(">7h", payload, 4)  # big-endian int16 x7

    frame = {
        "seq": seq,
        "ts_us": ts_us,
        "ax": ax, "ay": ay, "az": az,
        "gx": gx, "gy": gy, "gz": gz,
        "temp_c": temp / 340.0 + 36.53,  # MPU6050 datasheet formula
    }
    return frame, FRAME_LEN


def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
    except serial.SerialException as e:
        print(f"Could not open {PORT}: {e}")
        sys.exit(1)

    print(f"Listening on {PORT} at {BAUD} baud. Press Ctrl+C to stop.\n")

    buf = bytearray()
    last_seq = None
    frames_ok = 0
    frames_dropped = 0

    try:
        while True:
            data = ser.read(256)
            if data:
                buf.extend(data)

            # keep parsing as many complete frames as are sitting in buf
            while True:
                frame, consumed = try_parse_frame(bytes(buf))
                if consumed == 0:
                    break  # not enough bytes yet, go read more from serial
                del buf[:consumed]

                if frame is None:
                    frames_dropped += 1
                    continue  # resync case -- we advanced 1 byte, loop and try again

                frames_ok += 1

                # detect gaps in sequence number
                gap_note = ""
                if last_seq is not None:
                    expected = (last_seq + 1) & 0xFF
                    if frame["seq"] != expected:
                        gap_note = f"  <-- GAP (expected seq {expected})"
                last_seq = frame["seq"]

                print(
                    f"seq={frame['seq']:3d}  t={frame['ts_us']:>10d}us  "
                    f"accel=({frame['ax']:6d},{frame['ay']:6d},{frame['az']:6d})  "
                    f"gyro=({frame['gx']:6d},{frame['gy']:6d},{frame['gz']:6d})  "
                    f"temp={frame['temp_c']:5.1f}C{gap_note}"
                )

    except KeyboardInterrupt:
        print(f"\nStopped. Frames OK: {frames_ok}, resync/drop events: {frames_dropped}")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
