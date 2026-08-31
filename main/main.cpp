//   Wire.h (I2C)        -> driver/i2c_master.h (new-style I2C driver)
//   Serial (UART)       -> driver/uart.h (same as our hello_world UART code)
//   millis()/micros()   -> esp_timer_get_time() (returns int64_t microseconds since boot)
//   delay()             -> vTaskDelay(pdMS_TO_TICKS(ms))
//   digitalWrite        -> gpio_set_level() (after gpio_config() once at startup)
//   setup()/loop()      -> app_main() (loop() just deleted itself anyway, so we drop it)
//   FreeRTOS tasks/queues -> UNCHANGED, these are native ESP-IDF/FreeRTOS already

#include <cstdint>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define REG_PWR_MGMT_1     0x6B
#define REG_ACCEL_XOUT_H   0x3B
#define MPU_ADDR           0x68

#define I2C_SDA_PIN        GPIO_NUM_21
#define I2C_SCL_PIN        GPIO_NUM_22
#define I2C_FREQ_HZ        400000

#define UART_NUM           UART_NUM_0
#define UART_TX_PIN        1
#define UART_RX_PIN        3
#define UART_BAUD          115200

#define STATUS_LED_PIN     GPIO_NUM_2

struct ImuSample {
    uint32_t ts_us;
    int16_t ax, ay, az, temp, gx, gy, gz;
};

static QueueHandle_t sampleQueue;
static i2c_master_dev_handle_t mpu_dev;

// ---- I2C setup ----
static void i2c_init(void) {
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = I2C_SDA_PIN;
    bus_config.scl_io_num = I2C_SCL_PIN;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t bus_handle;
    i2c_new_master_bus(&bus_config, &bus_handle);

    i2c_device_config_t dev_config = {};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = MPU_ADDR;
    dev_config.scl_speed_hz = I2C_FREQ_HZ;

    i2c_master_bus_add_device(bus_handle, &dev_config, &mpu_dev);
}

// ---- mpuWriteReg() ----
static void mpuWriteReg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    i2c_master_transmit(mpu_dev, buf, 2, -1);
}

// ---- mpuReadAll() ----
static bool mpuReadAll(ImuSample &s) {
    uint8_t reg = REG_ACCEL_XOUT_H;
    uint8_t raw[14];

    // write register pointer, then read 14 bytes back (repeated-start, like Wire.endTransmission(false))
    if (i2c_master_transmit_receive(mpu_dev, &reg, 1, raw, 14, -1) != ESP_OK) {
        return false;
    }
    s.ts_us = (uint32_t)esp_timer_get_time(); // microseconds since boot, like micros()

    s.ax   = (raw[0]  << 8) | raw[1];
    s.ay   = (raw[2]  << 8) | raw[3];
    s.az   = (raw[4]  << 8) | raw[5];
    s.temp = (raw[6]  << 8) | raw[7];
    s.gx   = (raw[8]  << 8) | raw[9];
    s.gy   = (raw[10] << 8) | raw[11];
    s.gz   = (raw[12] << 8) | raw[13];
    return true;
}

// ---- CRC-16 ----
static uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
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
static volatile bool corruptMode = false;
static volatile uint32_t injectedErrors = 0;

// ---- Replaces sendFrame() — Serial.write() -> uart_write_bytes() ----
static void sendFrame(const ImuSample &s) {
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
        injectedErrors = injectedErrors + 1;
    }

    uart_write_bytes(UART_NUM, (const char*)f, 24);
}

// ---- Task 1: ----
static void taskSensor(void *pv) {
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

// ---- Task 2: ----
static void taskLink(void *pv) {
    ImuSample s;
    int64_t lastToggle = esp_timer_get_time() / 1000; // ms

    for (;;) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - lastToggle > 5000) {
            corruptMode = !corruptMode;
            lastToggle = now_ms;
            gpio_set_level(STATUS_LED_PIN, corruptMode ? 1 : 0);
        }
        if (xQueueReceive(sampleQueue, &s, portMAX_DELAY) == pdTRUE) {
            sendFrame(s);
        }
    }
}

extern "C" void app_main(void) {
    // ---- UART init ----
    uart_config_t uart_config = {};
    uart_config.baud_rate = UART_BAUD;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity    = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;

    uart_driver_install(UART_NUM, 1024, 0, 0, NULL, 0);
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // ---- GPIO init ----
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << STATUS_LED_PIN);
    io_conf.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io_conf);

    // ---- I2C + MPU6050 wake-up ----
    i2c_init();
    mpuWriteReg(REG_PWR_MGMT_1, 0x00);
    vTaskDelay(pdMS_TO_TICKS(100));

    // ---- Queue + tasks ----
    sampleQueue = xQueueCreate(32, sizeof(ImuSample));
    xTaskCreatePinnedToCore(taskSensor, "sensor", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(taskLink,   "link",   4096, NULL, 3, NULL, 1);

}
