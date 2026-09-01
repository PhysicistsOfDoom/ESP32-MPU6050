#pragma once
#include <cstdint>
#include "driver/i2c_master.h"
#include "driver/gpio.h"

struct ImuSample {
    uint32_t ts_us;
    int16_t ax, ay, az, temp, gx, gy, gz;
};

// Owns the I2C bus + device handle for one MPU6050 and knows how to wake
// it up and pull a full accel/temp/gyro burst read.
class Mpu6050 {
public:
    struct Config {
        gpio_num_t sda_pin = GPIO_NUM_21;
        gpio_num_t scl_pin = GPIO_NUM_22;
        uint32_t freq_hz = 400000;
        uint8_t addr = 0x68;
    };

    explicit Mpu6050(const Config &cfg = Config{});

    // Brings up the I2C bus (first call only) and clears the sensor's
    // sleep bit. Call once before readAll().
    void wake();

    // Reads all 14 bytes (accel + temp + gyro) into out. Returns false on
    // an I2C transaction error.
    bool readAll(ImuSample &out);

private:
    void writeReg(uint8_t reg, uint8_t val);

    Config cfg_;
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t dev_ = nullptr;

    static constexpr uint8_t REG_PWR_MGMT_1   = 0x6B;
    static constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
};
