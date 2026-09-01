idf_component_register(SRCS "main.cpp" "SensorNode.cpp" "Mpu6050.cpp" "FrameBuilder.cpp"
			PRIV_REQUIRES spi_flash esp_driver_uart esp_driver_i2c esp_driver_gpio esp_timer
                        INCLUDE_DIRS ".")
