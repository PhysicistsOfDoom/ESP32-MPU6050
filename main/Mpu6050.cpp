#include "Mpu6050.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

Mpu6050::Mpu6050(const Config &cfg) : cfg_(cfg) {}

void Mpu6050::wake() {
    if (!dev_) {
        i2c_master_bus_config_t bus_config = {};
        bus_config.i2c_port = I2C_NUM_0;
        bus_config.sda_io_num = cfg_.sda_pin;
        bus_config.scl_io_num = cfg_.scl_pin;
        bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_config.glitch_ignore_cnt = 7;
        bus_config.flags.enable_internal_pullup = true;
        i2c_new_master_bus(&bus_config, &bus_);

        i2c_device_config_t dev_config = {};
        dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_config.device_address = cfg_.addr;
        dev_config.scl_speed_hz = cfg_.freq_hz;
        i2c_master_bus_add_device(bus_, &dev_config, &dev_);
    }

    writeReg(REG_PWR_MGMT_1, 0x00);   // clear sleep bit
    vTaskDelay(pdMS_TO_TICKS(100));   // let the sensor settle
}

void Mpu6050::writeReg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    i2c_master_transmit(dev_, buf, 2, -1);
}

bool Mpu6050::readAll(ImuSample &out) {
    uint8_t reg = REG_ACCEL_XOUT_H;
    uint8_t raw[14];

    // write register pointer, then read 14 bytes back (repeated-start,
    // same idea as Wire.endTransmission(false))
    if (i2c_master_transmit_receive(dev_, &reg, 1, raw, 14, -1) != ESP_OK) {
        return false;
    }

    out.ts_us = (uint32_t)esp_timer_get_time();
    out.ax   = (raw[0]  << 8) | raw[1];
    out.ay   = (raw[2]  << 8) | raw[3];
    out.az   = (raw[4]  << 8) | raw[5];
    out.temp = (raw[6]  << 8) | raw[7];
    out.gx   = (raw[8]  << 8) | raw[9];
    out.gy   = (raw[10] << 8) | raw[11];
    out.gz   = (raw[12] << 8) | raw[13];
    return true;
}
