#include <Arduino.h>
#include <Wire.h>

#define REG_PWR_MGMT_1 0x6B
#define REG_ACCEL_XOUT_H 0x3B
#define MPU_ADDR 0x68

struct ImuSample {
	int16_t ax, ay, az, temp, gx, gy, gz;
};

bool mpuReadAll(ImuSample &s){
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(REG_ACCEL_XOUT_H);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)MPU_ADDR, (size_t)14, true) != 14) return false;

    s.ax   = (Wire.read() << 8) | Wire.read();
    s.ay   = (Wire.read() << 8) | Wire.read();
    s.az   = (Wire.read() << 8) | Wire.read();
    s.temp = (Wire.read() << 8) | Wire.read();
    s.gx   = (Wire.read() << 8) | Wire.read();
    s.gy   = (Wire.read() << 8) | Wire.read();
    s.gz   = (Wire.read() << 8) | Wire.read();
    return true;
}

void mpuWriteReg(uint8_t reg, uint8_t val) {
	Wire.beginTransmission(MPU_ADDR);
	Wire.write(reg);
	Wire.write(val);
	Wire.endTransmission(true);
}

void setup() {
	Serial.begin(115200);
	Serial.println("boot ok");
	Wire.begin(21,22);
	mpuWriteReg(REG_PWR_MGMT_1, 0x00);
	delay(100);
}


// Central loop
void loop() {
	ImuSample s;
	if (mpuReadAll(s)) {
		Serial.printf("ax=%d ay=%d az=%d temp=%d gx=%d gy=%d gz=%d\n",
			s.ax, s.ay, s.az, s.temp, s.gx, s.gy, s.gz);	
	}
	delay(100);
}

