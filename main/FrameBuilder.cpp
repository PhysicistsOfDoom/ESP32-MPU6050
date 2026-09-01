#include "FrameBuilder.h"

uint8_t FrameBuilder::build(const ImuSample &s, bool corrupt, uint8_t out[kFrameSize]) {
    uint8_t thisSeq = seq_;
    seq_++;

    out[0] = 0xAA;
    out[1] = 0x55;
    out[2] = 18;        // LEN
    out[3] = thisSeq;   // SEQ

    out[4] = s.ts_us & 0xFF;
    out[5] = (s.ts_us >> 8) & 0xFF;
    out[6] = (s.ts_us >> 16) & 0xFF;
    out[7] = (s.ts_us >> 24) & 0xFF;

    const int16_t vals[7] = { s.ax, s.ay, s.az, s.temp, s.gx, s.gy, s.gz };
    for (int i = 0; i < 7; i++) {
        out[8 + i * 2]     = (vals[i] >> 8) & 0xFF;
        out[8 + i * 2 + 1] =  vals[i]       & 0xFF;
    }

    uint16_t crc = crc16_ccitt(&out[2], 20);  // LEN + SEQ + PAYLOAD only
    out[22] = (crc >> 8) & 0xFF;
    out[23] =  crc       & 0xFF;

    // matches the original's check: it compared the counter *after* the
    // increment, so this triggers on the frame right before every 10th seq
    if (corrupt && (seq_ % 10 == 0)) {
        out[12] ^= 0x01;
        injectedErrors_++;
    }

    return thisSeq;
}

uint16_t FrameBuilder::crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}
