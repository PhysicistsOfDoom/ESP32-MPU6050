#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "Mpu6050.h"
#include "FrameBuilder.h"

// Owns the sensor, the framer, the UART link, and the two FreeRTOS tasks
// that connect them (sensor task = producer, link task = consumer).
//
// Note: the FreeRTOS queue between the two tasks already gives us the
// mutex+semaphore behavior you'd hand-roll with std::mutex +
// std::counting_semaphore - xQueueSend/xQueueReceive are internally
// locked, and xQueueReceive(..., portMAX_DELAY) already blocks the
// consumer until data shows up. Nothing extra to add there.
class SensorNode {
public:
    struct Config {
        gpio_num_t status_led_pin = GPIO_NUM_2;
        uart_port_t uart_num = UART_NUM_0;
        int uart_tx_pin = 1;
        int uart_rx_pin = 3;
        int uart_baud = 115200;
        TickType_t sample_period = pdMS_TO_TICKS(10);
        int64_t corrupt_toggle_ms = 5000;
    };

    explicit SensorNode(const Config &cfg = Config{});

    // Sets up UART/GPIO/I2C, wakes the sensor, and spawns the sensor +
    // link tasks. Call once from app_main().
    void begin();

private:
    // FreeRTOS wants a plain void(*)(void*) function pointer, so these
    // trampolines just cast pv back to a SensorNode* and forward into the
    // real per-instance loop below.
    static void sensorTaskTrampoline(void *pv);
    static void linkTaskTrampoline(void *pv);

    void sensorTaskLoop();
    void linkTaskLoop();

    void initUart();
    void initGpio();

    Config cfg_;
    Mpu6050 imu_;
    FrameBuilder framer_;
    QueueHandle_t queue_ = nullptr;
    bool corruptMode_ = false;
};
