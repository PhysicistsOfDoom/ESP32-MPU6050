# ESP32 + MPU6050 Project

An ESP32 samples an MPU6050 IMU at 100 Hz on a dedicated FreeRTOS task, packages
each sample into a fixed-size framed packet (sync word, sequence number,
payload, CRC-16), and streams it over UART. A Python host script (`frame_parser.py`)
re-syncs on the byte stream, validates every frame's CRC, detects sequence
gaps, and prints a running frames-OK/dropped count.

The firmware also has a built-in test mode that deliberately corrupts frames
on a timer, so the error-detection logic isn't just assumed to work — it's
proven against real, injected errors.

Originally built on PlatformIO/Arduino; ported to raw ESP-IDF (v6.1) to work
directly against the native FreeRTOS/driver APIs rather than the Arduino
abstraction layer.

## Design

`main/` is split into one class per responsibility instead of one file full
of free functions and globals:

- **`Mpu6050`** (`Mpu6050.h`/`.cpp`) — owns the I2C bus/device handle and the
  register-level protocol: `wake()` brings up the I2C bus on first call and
  clears the sensor's sleep bit, and `readAll()` pulls all 7 axes +
  temperature in one transaction.
- **`FrameBuilder`** (`FrameBuilder.h`/`.cpp`) — owns everything about the
  wire format: the rolling sequence number, the CRC-16-CCITT computation, and
  the periodic error-injection ("corrupt mode") behavior, all behind a single
  `build()` call.
- **`SensorNode`** (`SensorNode.h`/`.cpp`) — owns one `Mpu6050`, one
  `FrameBuilder`, the FreeRTOS queue between them, and the UART/GPIO link.
  `begin()` sets up UART/GPIO, wakes the sensor, and spawns the sensor and
  link tasks.

The two FreeRTOS tasks stay plain functions — `xTaskCreatePinnedToCore`
requires a `void(*)(void*)` pointer, so they can't be instance methods
directly — but each is just a one-line static trampoline
(`sensorTaskTrampoline`/`linkTaskTrampoline`) that casts `pv` back to a
`SensorNode*` and forwards into the real per-instance loop
(`sensorTaskLoop()`/`linkTaskLoop()`). The queue between them is a raw
FreeRTOS `QueueHandle_t` living inside `SensorNode` — its built-in locking
already gives blocking producer/consumer semantics, so there's no separate
wrapper needed once nothing outside `SensorNode` touches it.

`main/main.cpp` itself is just:

```cpp
#include "SensorNode.h"

extern "C" void app_main(void) {
    static SensorNode node;
    node.begin();
}
```

## Hardware

| MPU6050 pin | ESP32 pin | Note |
|---|---|---|
| VCC | 3V3 | Not 5V |
| GND | GND | |
| SDA | GPIO 21 | |
| SCL | GPIO 22 | |
| AD0 | GND | Sets I2C address to 0x68 |

## Frame format — 24 bytes

```
Offset  Size  Field    Description
0       2     SYNC     0xAA 0x55 — used to re-find frame boundaries
2       1     LEN      payload length (always 18)
3       1     SEQ      wraps 0-255, used to detect loss
4       18    PAYLOAD  4-byte timestamp + 7x int16 sensor values
22      2     CRC16    CRC-16-CCITT over LEN+SEQ+PAYLOAD
```

## Running it

**Firmware (ESP-IDF):**
```
idf.py build flash monitor
```

**Host receiver:**

`pip install -r requirements.txt`:

```
python3 frame_parser.py /dev/ttyUSB0
```
Ctrl+C prints a final frames-OK/resync-drop count.

> `run.sh` builds, flashes, waits for the board to reboot, then launches
> `frame_parser.py` on the given port (`--no-build` skips the build/flash
> step, `--port=` overrides the default `/dev/ttyUSB0`).
>
> `hex_monitor.py` is a raw hex dump of whatever bytes show up on the
> serial port — no framing/CRC logic at all — useful for a quick sanity
> check that the board is transmitting anything before debugging the
> framed protocol.

## Stats (CRC, Link stats, Jitter):

> Captured with an earlier, more instrumented host receiver (`link_rx.py`,
> since removed) that tracked live link state and duplicate/jitter stats on
> top of the same CRC/sequence validation `frame_parser.py` still does.
> `frame_parser.py` only prints a running frames-OK/resync-drop count, but
> the underlying firmware behavior and measurements below are unchanged.

![Link state transitioning UP to DEGRADED and back](docs/images/2.%20LinkState.png)

Every 5 seconds the firmware flips into "corrupt mode" and flips one bit in
1-of-10 outgoing frames — after the CRC is already computed, so it's a real
simulated transmission error. The receiver's job is to catch every one of
them without ever flagging a clean frame.

Measured result from a live run:

![Successful test run](docs/images/4.%20SuccessfulTests.png)

```
--- LINK STATISTICS ---
elapsed        : 15.6 s
frames good    : 1499  (95.8 fps)
CRC failures   : 65
sequence gaps  : 64  (frames lost: 64)
duplicates     : 0
frame error rate: 4.16%

--- SAMPLE INTERVAL (jitter) ---
mean           : 10427.2 us
min / max      : 9999 / 20001 us
stddev         : 2023.0 us
```

Every injected error was caught by the CRC (65 failures line up with the
64 sequence gaps that follow them), zero clean frames were ever flagged, and
zero duplicates. The jitter mean/max are pulled up by the dropped-frame
windows (a missed sample reads as ~20000us, double the 10000us period) —
the underlying sample timing itself holds a steady 9999-10000us baseline.

## Known limitations

- Detection only — no forward error correction or retransmission.
- No authentication/encryption on the link.
- Fixed 115200 baud.
- 8-bit sequence number wraps every 256 frames.

## Bench setup

![Full setup](docs/images/Fullview.png)

![Wiring closeup](docs/images/Closeup.png)
