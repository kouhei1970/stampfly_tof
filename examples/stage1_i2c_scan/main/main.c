/**
 * @file main.c
 * @brief Stage 1: I2C Bus Scan - VL53L3CX Device Detection
 *
 * This example scans the I2C bus to detect VL53L3CX ToF sensors.
 * Expected: Device found at address 0x29 (VL53L3CX default 7-bit address)
 *
 * Hardware Setup:
 * - I2C SDA: GPIO3
 * - I2C SCL: GPIO4
 * - Front ToF XSHUT: GPIO9 (set HIGH to enable sensor)
 * - Bottom ToF XSHUT: GPIO7 (set LOW to disable sensor)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "stampfly_tof_config.h"

static const char *TAG = "I2C_SCAN";

/**
 * @brief Initialize I2C master
 */
static esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = STAMPFLY_I2C_SDA_GPIO,
        .scl_io_num = STAMPFLY_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = STAMPFLY_I2C_FREQ_HZ,
    };

    esp_err_t err = i2c_param_config(STAMPFLY_I2C_PORT, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(STAMPFLY_I2C_PORT, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C master initialized successfully");
    ESP_LOGI(TAG, "SDA: GPIO%d, SCL: GPIO%d, Freq: %d Hz",
             STAMPFLY_I2C_SDA_GPIO, STAMPFLY_I2C_SCL_GPIO, STAMPFLY_I2C_FREQ_HZ);

    return ESP_OK;
}

/**
 * @brief Initialize XSHUT pins for ToF sensors
 */
static void tof_xshut_init(void)
{
    // Configure XSHUT pins as outputs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << STAMPFLY_TOF_FRONT_XSHUT) |
                        (1ULL << STAMPFLY_TOF_BOTTOM_XSHUT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Enable front sensor, disable bottom sensor
    gpio_set_level(STAMPFLY_TOF_FRONT_XSHUT, 1);
    gpio_set_level(STAMPFLY_TOF_BOTTOM_XSHUT, 0);

    ESP_LOGI(TAG, "XSHUT pins initialized");
    ESP_LOGI(TAG, "Front ToF (GPIO%d): ENABLED", STAMPFLY_TOF_FRONT_XSHUT);
    ESP_LOGI(TAG, "Bottom ToF (GPIO%d): DISABLED", STAMPFLY_TOF_BOTTOM_XSHUT);

    // Wait for sensor to boot
    vTaskDelay(pdMS_TO_TICKS(50));
}

/**
 * @brief Scan I2C bus for devices
 */
static void i2c_scan(void)
{
    ESP_LOGI(TAG, "Starting I2C bus scan...");
    ESP_LOGI(TAG, "Scanning address range: 0x03 to 0x77");

    int devices_found = 0;

    for (uint8_t addr = 0x03; addr < 0x78; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);

        esp_err_t ret = i2c_master_cmd_begin(STAMPFLY_I2C_PORT, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Device found at address 0x%02X", addr);
            devices_found++;

            // Check if it's the expected VL53L3CX address
            if (addr == VL53L3CX_DEFAULT_I2C_ADDR) {
                ESP_LOGI(TAG, "  -> VL53L3CX detected at default address!");
            }
        } else if (ret == ESP_ERR_TIMEOUT) {
            // No device at this address (this is normal)
        } else {
            ESP_LOGW(TAG, "Error at address 0x%02X: %s", addr, esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "I2C scan completed. Devices found: %d", devices_found);

    if (devices_found == 0) {
        ESP_LOGW(TAG, "No I2C devices found! Please check:");
        ESP_LOGW(TAG, "  - I2C wiring (SDA, SCL)");
        ESP_LOGW(TAG, "  - Pull-up resistors");
        ESP_LOGW(TAG, "  - Sensor power supply");
        ESP_LOGW(TAG, "  - XSHUT pin levels");
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "==================================");
    ESP_LOGI(TAG, "Stage 1: I2C Bus Scan");
    ESP_LOGI(TAG, "VL53L3CX Device Detection Test");
    ESP_LOGI(TAG, "==================================");

    // Initialize XSHUT pins
    tof_xshut_init();

    // Initialize I2C
    esp_err_t ret = i2c_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C initialization failed!");
        return;
    }

    // Scan I2C bus
    i2c_scan();

    ESP_LOGI(TAG, "Test completed. You can now flash Stage 2.");
}
