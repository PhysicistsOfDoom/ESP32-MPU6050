import serial, struct, sys, time, statistics
from collections import deque

SYNC1, SYNC2 = 0xAA, 0x55
WINDOW = 50
DOWN_TIMEOUT = 0.5

class LinkState:
    def __init__(self):
        self.state = 'DOWN'
        self.outcomes = deque(maxlen=WINDOW)
        self.last_good = None

    def record(self, good, gap=False):
        self.outcomes.append(good)
        if good:
            self.last_good = time.time()
        self._update(gap)

    def check_timeout(self):
        self._update(False)

    def _update(self, gap):
        if self.last_good is None or (time.time() - self.last_good) > DOWN_TIMEOUT:
            new_state = 'DOWN'
        else:
            error_rate = 1 - (sum(self.outcomes) / len(self.outcomes)) if self.outcomes else 0
            new_state = 'DEGRADED' if (error_rate >= 0.01 or gap) else 'UP'
        if new_state != self.state:
            print(f"  ### LINK STATE: {self.state} -> {new_state}")
            self.state = new_state

class LinkStats:
    def __init__(self):
        self.frames = self.crc_errors = self.seq_gaps = self.lost = 0
        self.duplicates = 0
        self.last_ts = None
        self.intervals = []
        self.start = time.time()

    def report(self):
        el = time.time() - self.start
        print(f"\n--- LINK STATISTICS ---")
        print(f"elapsed        : {el:.1f} s")
        print(f"frames good    : {self.frames}  ({self.frames/el:.1f} fps)")
        print(f"CRC failures   : {self.crc_errors}")
        print(f"sequence gaps  : {self.seq_gaps}  (frames lost: {self.lost})")
        print(f"duplicates     : {self.duplicates}")
        total = self.frames + self.crc_errors
        if total:
            print(f"frame error rate: {100*self.crc_errors/total:.2f}%")
        if len(self.intervals) > 1:
            print(f"\n--- SAMPLE INTERVAL (jitter) ---")
            print(f"mean           : {statistics.mean(self.intervals):.1f} us")
            print(f"min / max      : {min(self.intervals)} / {max(self.intervals)} us")
            print(f"stddev         : {statistics.stdev(self.intervals):.1f} us")


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
    last_seq, last_ts = None, None
    link = LinkState()
    st = LinkStats()
    try:
        while True:
            byte = ser.read(1)
            if not byte:
                link.check_timeout()
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
                        if seq == last_seq and ts == last_ts:
                            st.duplicates += 1
                            state = 'SYNC1'
                            continue

                        if last_seq is not None:
                            gap = (seq - last_seq) & 0xFF
                            if gap != 1:
                                st.seq_gaps += 1
                                st.lost += gap - 1
                                print(f"  !! SEQ GAP — expected {(last_seq + 1) & 0xFF}, got {seq} ({gap - 1} lost)")
                            link.record(good=True, gap=(gap !=1))
                        else:    
                            link.record(good=True)
                        last_seq, last_ts = seq, ts
                        st.frames += 1
                        if st.last_ts is not None:
                            st.intervals.append((ts - st.last_ts) & 0xFFFFFFFF)
                        st.last_ts = ts
                        
                        ax, ay, az, tmp, gx, gy, gz = struct.unpack('>7h', bytes(body[6:20]))
                        print(f"seq={seq:3d} ts={ts:10d} ax={ax:6d} ay={ay:6d} az={az:6d} "
                            f"gx={gx:6d} gy={gy:6d} gz={gz:6d}")
                    else:
                        link.record(good=False)
                        print("!! CRC FAIL — frame discarded")
                        st.crc_errors += 1
                    state = 'SYNC1'
    except KeyboardInterrupt:
        st.report()

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyUSB0')