#pragma once
#include <cstdint>
#include <cstddef>
#include "Mpu6050.h"

// Encodes one ImuSample into the wire frame:
//   [0xAA][0x55][LEN][SEQ][ts_us x4][ax ay az temp gx gy gz, big-endian int16][CRC16 x2]
// Also owns the running sequence counter and the optional fault injector,
// since both are properties of "building a frame" rather than the link.
class FrameBuilder {
public:
    static constexpr size_t kFrameSize = 24;

    // Writes kFrameSize bytes into out. Returns the sequence number used.
    // If corrupt is true, occasionally flips a payload bit to simulate a
    // noisy link (same 1-in-10 pattern as the original code).
    uint8_t build(const ImuSample &s, bool corrupt, uint8_t out[kFrameSize]);

    uint32_t injectedErrors() const { return injectedErrors_; }

private:
    static uint16_t crc16_ccitt(const uint8_t *data, size_t len);

    uint8_t seq_ = 0;
    uint32_t injectedErrors_ = 0;
};
