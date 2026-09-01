#include "SensorNode.h"
#include "esp_timer.h"

SensorNode::SensorNode(const Config &cfg) : cfg_(cfg) {}

void SensorNode::initUart() {
    uart_config_t uart_config = {};
    uart_config.baud_rate = cfg_.uart_baud;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity    = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;

    uart_driver_install(cfg_.uart_num, 1024, 0, 0, NULL, 0);
    uart_param_config(cfg_.uart_num, &uart_config);
    uart_set_pin(cfg_.uart_num, cfg_.uart_tx_pin, cfg_.uart_rx_pin,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void SensorNode::initGpio() {
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << cfg_.status_led_pin);
    io_conf.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io_conf);
}

void SensorNode::begin() {
    initUart();
    initGpio();
    imu_.wake();

    queue_ = xQueueCreate(32, sizeof(ImuSample));

    xTaskCreatePinnedToCore(&SensorNode::sensorTaskTrampoline, "sensor", 4096, this, 5, NULL, 1);
    xTaskCreatePinnedToCore(&SensorNode::linkTaskTrampoline,   "link",   4096, this, 3, NULL, 1);
}

void SensorNode::sensorTaskTrampoline(void *pv) {
    static_cast<SensorNode *>(pv)->sensorTaskLoop();
}

void SensorNode::linkTaskTrampoline(void *pv) {
    static_cast<SensorNode *>(pv)->linkTaskLoop();
}

// Producer: samples the IMU on a fixed period and drops each reading onto
// the queue. A full queue just means readAll() ran ahead of the link task -
// xQueueSend(..., 0) will simply skip that sample rather than block.
void SensorNode::sensorTaskLoop() {
    TickType_t last = xTaskGetTickCount();
    ImuSample s;
    for (;;) {
        if (imu_.readAll(s)) {
            xQueueSend(queue_, &s, 0);
        }
        vTaskDelayUntil(&last, cfg_.sample_period);
    }
}

// Consumer: blocks on the queue, builds a frame for whatever shows up, and
// pushes it out over UART. Also flips the "simulate a bad link" state on a
// timer, toggling the status LED to match.
void SensorNode::linkTaskLoop() {
    ImuSample s;
    int64_t lastToggle = esp_timer_get_time() / 1000;

    for (;;) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - lastToggle > cfg_.corrupt_toggle_ms) {
            corruptMode_ = !corruptMode_;
            lastToggle = now_ms;
            gpio_set_level(cfg_.status_led_pin, corruptMode_ ? 1 : 0);
        }

        if (xQueueReceive(queue_, &s, portMAX_DELAY) == pdTRUE) {
            uint8_t frame[FrameBuilder::kFrameSize];
            framer_.build(s, corruptMode_, frame);
            uart_write_bytes(cfg_.uart_num, (const char *)frame, FrameBuilder::kFrameSize);
        }
    }
}
