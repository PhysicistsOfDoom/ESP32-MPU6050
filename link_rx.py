import serial, struct, sys

SYNC1, SYNC2 = 0xAA, 0x55

def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc

def main(port):
    ser = serial.Serial(port, 115200, timeout=1)
    state, buf, need = 'SYNC1', bytearray(), 0
    while True:
        byte = ser.read(1)
        if not byte:
            continue
        b = byte[0]

        if state == 'SYNC1':
            if b == SYNC1: state = 'SYNC2'
        elif state == 'SYNC2':
            state = 'BODY' if b == SYNC2 else ('SYNC2' if b == SYNC1 else 'SYNC1')
            buf, need = bytearray(), 22
        elif state == 'BODY':
            buf.append(b)
            if len(buf) == need:
                body, rx_crc = buf[:20], (buf[20] << 8) | buf[21]
                if crc16_ccitt(bytes(body)) == rx_crc:
                    seq = body[1]
                    ts, = struct.unpack('<I', bytes(body[2:6]))
                    ax, ay, az, tmp, gx, gy, gz = struct.unpack('>7h', bytes(body[6:20]))
                    print(f"seq={seq:3d} ts={ts:10d} ax={ax:6d} ay={ay:6d} az={az:6d} "
                        f"gx={gx:6d} gy={gy:6d} gz={gz:6d}")
                else:
                    print("!! CRC FAIL — frame discarded")
                state = 'SYNC1'

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyUSB0')