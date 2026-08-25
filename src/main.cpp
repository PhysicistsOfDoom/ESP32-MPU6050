#include <Arduino.h>
#include <Wire.h>

#define REG_PWR_MGMT_1 0x6B
#define REG_ACCEL_XOUT_H 0x3B // REAL sensor data contiguously begins!!
#define MPU_ADDR 0x68

struct ImuSample {
	uint32_t ts_us;
	int16_t ax, ay, az, temp, gx, gy, gz;
};

QueueHandle_t sampleQueue;

bool mpuReadAll(ImuSample &s){
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(REG_ACCEL_XOUT_H);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)MPU_ADDR, (size_t)14, true) != 14) return false;
	s.ts_us = micros();

    s.ax   = (Wire.read() << 8) | Wire.read();
    s.ay   = (Wire.read() << 8) | Wire.read();
    s.az   = (Wire.read() << 8) | Wire.read();
    s.temp = (Wire.read() << 8) | Wire.read();
    s.gx   = (Wire.read() << 8) | Wire.read();
    s.gy   = (Wire.read() << 8) | Wire.read();
    s.gz   = (Wire.read() << 8) | Wire.read();
    return true;
}

// WAKE UP the MPU!
void mpuWriteReg(uint8_t reg, uint8_t val) {
	Wire.beginTransmission(MPU_ADDR);
	Wire.write(reg);
	Wire.write(val);
	Wire.endTransmission(true);
}

// CRC-16 function
uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
	uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
      crc ^= (uint16_t)data[i] << 8;
      for (uint8_t b = 0; b < 8; b++) {
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
      }
    }
    return crc;
}

static uint8_t txSeq = 0;
volatile bool corruptMode = false;
volatile uint32_t injectedErrors = 0;

void sendFrame(const ImuSample &s) {
    uint8_t f[24];
    f[0] = 0xAA;  f[1] = 0x55;
    f[2] = 18;                    // LEN
    f[3] = txSeq++;               // SEQ, wraps naturally at 255
     
    f[4] = s.ts_us & 0xFF;
    f[5] = (s.ts_us >> 8) & 0xFF;
    f[6] = (s.ts_us >> 16) & 0xFF;
    f[7] = (s.ts_us >> 24) & 0xFF;
     
    const int16_t vals[7] = { s.ax, s.ay, s.az, s.temp, s.gx, s.gy, s.gz };
    for (int i = 0; i < 7; i++) {
      f[8 + i*2]     = (vals[i] >> 8) & 0xFF;
      f[8 + i*2 + 1] =  vals[i]       & 0xFF;
    }

    uint16_t crc = crc16_ccitt(&f[2], 20);   // LEN+SEQ+PAYLOAD only
    f[22] = (crc >> 8) & 0xFF;
    f[23] =  crc       & 0xFF;

    if (corruptMode && (txSeq % 10 == 0)) {
      f[12] ^= 0x01;
      injectedErrors++;
    }
          
    Serial.write(f, 24);
}


void taskSensor (void *pv) {
	const TickType_t period = pdMS_TO_TICKS(10); 
	TickType_t last = xTaskGetTickCount();
	ImuSample s;
	for (;;) {
		if (mpuReadAll(s)) {
			xQueueSend(sampleQueue, &s, 0);
		}
		vTaskDelayUntil(&last, period);
	}
}

void taskLink(void *pv) {
	ImuSample s;
	uint32_t lastToggle = millis();
	for (;;) {
	    if (millis() - lastToggle > 5000) {
	      corruptMode = !corruptMode;
	      lastToggle = millis();
	      digitalWrite(2, corruptMode ? HIGH : LOW);
	    }
		if (xQueueReceive(sampleQueue, &s, portMAX_DELAY) == pdTRUE) {
			sendFrame(s);
		}
	}
}

void setup() {
	Serial.begin(115200);
	Serial.println("boot ok");
	Wire.begin(21,22);
	mpuWriteReg(REG_PWR_MGMT_1, 0x00);
	delay(100);

	sampleQueue = xQueueCreate(32, sizeof(ImuSample));
	xTaskCreatePinnedToCore(taskSensor, "sensor", 4096, NULL, 5, NULL, 1); // Both of these will be context switching on core 1
	xTaskCreatePinnedToCore(taskLink, "link", 4096, NULL, 3, NULL, 1);
}

// Central loop
void loop() {
	vTaskDelete(NULL);
}

