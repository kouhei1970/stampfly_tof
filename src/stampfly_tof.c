/**
 * @file stampfly_tof.c
 * @brief StampFly ToF Sensor Integration Layer Implementation
 */

#include "stampfly_tof.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "STAMPFLY_TOF";

// Static handle pointers for ISR access
static stampfly_tof_handle_t *g_front_handle = NULL;
static stampfly_tof_handle_t *g_bottom_handle = NULL;

/**
 * @brief Initialize GPIO pins for ToF sensors
 */
static esp_err_t stampfly_tof_gpio_init(void)
{
    ESP_LOGI(TAG, "Initializing GPIO pins...");

    // Configure XSHUT pins as output
    gpio_config_t io_conf_xshut = {
        .pin_bit_mask = (1ULL << STAMPFLY_TOF_FRONT_XSHUT_PIN) |
                        (1ULL << STAMPFLY_TOF_BOTTOM_XSHUT_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf_xshut);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure XSHUT pins");
        return ret;
    }

    // Configure INT pins as input
    gpio_config_t io_conf_int = {
        .pin_bit_mask = (1ULL << STAMPFLY_TOF_FRONT_INT_PIN) |
                        (1ULL << STAMPFLY_TOF_BOTTOM_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&io_conf_int);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure INT pins");
        return ret;
    }

    ESP_LOGI(TAG, "GPIO pins configured");
    return ESP_OK;
}

esp_err_t stampfly_tof_set_xshut(stampfly_tof_sensor_t sensor, uint8_t level)
{
    esp_err_t ret = ESP_OK;

    if (sensor == STAMPFLY_TOF_SENSOR_FRONT || sensor == STAMPFLY_TOF_SENSOR_BOTH) {
        ret = gpio_set_level(STAMPFLY_TOF_FRONT_XSHUT_PIN, level);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set front XSHUT");
            return ret;
        }
    }

    if (sensor == STAMPFLY_TOF_SENSOR_BOTTOM || sensor == STAMPFLY_TOF_SENSOR_BOTH) {
        ret = gpio_set_level(STAMPFLY_TOF_BOTTOM_XSHUT_PIN, level);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set bottom XSHUT");
            return ret;
        }
    }

    return ESP_OK;
}

esp_err_t stampfly_tof_get_int_pin(stampfly_tof_sensor_t sensor, uint8_t *level)
{
    if (level == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (sensor == STAMPFLY_TOF_SENSOR_FRONT) {
        *level = gpio_get_level(STAMPFLY_TOF_FRONT_INT_PIN);
    } else if (sensor == STAMPFLY_TOF_SENSOR_BOTTOM) {
        *level = gpio_get_level(STAMPFLY_TOF_BOTTOM_INT_PIN);
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t stampfly_tof_init(stampfly_tof_handle_t *handle, i2c_port_t i2c_port)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing StampFly ToF system...");

    handle->i2c_port = i2c_port;
    handle->initialized = false;

    // Step 1: Initialize I2C master
    esp_err_t ret = vl53l3cx_i2c_master_init(i2c_port,
                                              STAMPFLY_TOF_I2C_SDA_PIN,
                                              STAMPFLY_TOF_I2C_SCL_PIN,
                                              STAMPFLY_TOF_I2C_FREQ_HZ);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C initialization failed");
        return ret;
    }

    // Step 2: Initialize GPIO pins
    ret = stampfly_tof_gpio_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO initialization failed");
        return ret;
    }

    // Step 3: Shutdown all sensors
    ESP_LOGI(TAG, "Shutting down all sensors...");
    ret = stampfly_tof_set_xshut(STAMPFLY_TOF_SENSOR_BOTH, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));  // Wait for shutdown

    // ========================================
    // Step 4: Initialize Front Sensor
    // ========================================
    ESP_LOGI(TAG, "Initializing front sensor...");

    // Wake up front sensor only
    ret = stampfly_tof_set_xshut(STAMPFLY_TOF_SENSOR_FRONT, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));  // Wait for sensor boot

    // Initialize with default address (0x29)
    ret = vl53l3cx_init(&handle->front_sensor, i2c_port, VL53L3CX_DEFAULT_I2C_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Front sensor initialization failed");
        return ret;
    }

    // Change I2C address to avoid conflict
    ret = vl53l3cx_set_device_address(&handle->front_sensor, STAMPFLY_TOF_FRONT_I2C_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Front sensor address change failed");
        return ret;
    }

    ESP_LOGI(TAG, "Front sensor initialized at 0x%02X", STAMPFLY_TOF_FRONT_I2C_ADDR);

    // ========================================
    // Step 5: Initialize Bottom Sensor
    // ========================================
    ESP_LOGI(TAG, "Initializing bottom sensor...");

    // Wake up bottom sensor
    ret = stampfly_tof_set_xshut(STAMPFLY_TOF_SENSOR_BOTTOM, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    // Initialize with default address (0x29)
    ret = vl53l3cx_init(&handle->bottom_sensor, i2c_port, VL53L3CX_DEFAULT_I2C_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bottom sensor initialization failed");
        return ret;
    }

    // Change I2C address
    ret = vl53l3cx_set_device_address(&handle->bottom_sensor, STAMPFLY_TOF_BOTTOM_I2C_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bottom sensor address change failed");
        return ret;
    }

    ESP_LOGI(TAG, "Bottom sensor initialized at 0x%02X", STAMPFLY_TOF_BOTTOM_I2C_ADDR);

    handle->initialized = true;
    ESP_LOGI(TAG, "StampFly ToF system initialization complete");

    return ESP_OK;
}

esp_err_t stampfly_tof_deinit(stampfly_tof_handle_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Deinitializing StampFly ToF system...");

    // Stop ranging on both sensors
    if (handle->initialized) {
        stampfly_tof_stop_ranging(handle, STAMPFLY_TOF_SENSOR_BOTH);
    }

    // Shutdown all sensors
    stampfly_tof_set_xshut(STAMPFLY_TOF_SENSOR_BOTH, 0);

    // Deinitialize I2C
    esp_err_t ret = vl53l3cx_i2c_master_deinit(handle->i2c_port);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C deinitialization failed");
    }

    handle->initialized = false;
    ESP_LOGI(TAG, "StampFly ToF system deinitialized");

    return ESP_OK;
}

esp_err_t stampfly_tof_start_ranging(stampfly_tof_handle_t *handle, stampfly_tof_sensor_t sensor)
{
    if (handle == NULL || !handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_OK;

    if (sensor == STAMPFLY_TOF_SENSOR_FRONT || sensor == STAMPFLY_TOF_SENSOR_BOTH) {
        ret = vl53l3cx_start_ranging(&handle->front_sensor);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start front sensor ranging");
            return ret;
        }
    }

    if (sensor == STAMPFLY_TOF_SENSOR_BOTTOM || sensor == STAMPFLY_TOF_SENSOR_BOTH) {
        ret = vl53l3cx_start_ranging(&handle->bottom_sensor);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start bottom sensor ranging");
            return ret;
        }
    }

    ESP_LOGI(TAG, "Ranging started on sensor %d", sensor);
    return ESP_OK;
}

esp_err_t stampfly_tof_stop_ranging(stampfly_tof_handle_t *handle, stampfly_tof_sensor_t sensor)
{
    if (handle == NULL || !handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_OK;

    if (sensor == STAMPFLY_TOF_SENSOR_FRONT || sensor == STAMPFLY_TOF_SENSOR_BOTH) {
        ret = vl53l3cx_stop_ranging(&handle->front_sensor);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop front sensor ranging");
        }
    }

    if (sensor == STAMPFLY_TOF_SENSOR_BOTTOM || sensor == STAMPFLY_TOF_SENSOR_BOTH) {
        ret = vl53l3cx_stop_ranging(&handle->bottom_sensor);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop bottom sensor ranging");
        }
    }

    ESP_LOGI(TAG, "Ranging stopped on sensor %d", sensor);
    return ESP_OK;
}

esp_err_t stampfly_tof_get_front_distance(stampfly_tof_handle_t *handle, vl53l3cx_result_t *result)
{
    if (handle == NULL || !handle->initialized || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Wait for data ready
    esp_err_t ret = vl53l3cx_wait_data_ready(&handle->front_sensor, VL53L3CX_RANGING_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Front sensor data ready timeout");
        return ret;
    }

    // Get ranging data
    ret = vl53l3cx_get_ranging_data(&handle->front_sensor, result);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get front sensor data");
        return ret;
    }

    return ESP_OK;
}

esp_err_t stampfly_tof_get_bottom_distance(stampfly_tof_handle_t *handle, vl53l3cx_result_t *result)
{
    if (handle == NULL || !handle->initialized || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Wait for data ready
    esp_err_t ret = vl53l3cx_wait_data_ready(&handle->bottom_sensor, VL53L3CX_RANGING_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Bottom sensor data ready timeout");
        return ret;
    }

    // Get ranging data
    ret = vl53l3cx_get_ranging_data(&handle->bottom_sensor, result);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get bottom sensor data");
        return ret;
    }

    return ESP_OK;
}

esp_err_t stampfly_tof_get_dual_distance(stampfly_tof_handle_t *handle, stampfly_tof_dual_result_t *result)
{
    if (handle == NULL || !handle->initialized || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    vl53l3cx_result_t front_result, bottom_result;

    // Get front sensor data
    esp_err_t ret = stampfly_tof_get_front_distance(handle, &front_result);
    if (ret == ESP_OK) {
        result->front_distance_mm = front_result.distance_mm;
        result->front_status = front_result.range_status;
    } else {
        result->front_distance_mm = 0;
        result->front_status = VL53L3CX_RANGE_STATUS_RANGE_INVALID;
    }

    // Get bottom sensor data
    ret = stampfly_tof_get_bottom_distance(handle, &bottom_result);
    if (ret == ESP_OK) {
        result->bottom_distance_mm = bottom_result.distance_mm;
        result->bottom_status = bottom_result.range_status;
    } else {
        result->bottom_distance_mm = 0;
        result->bottom_status = VL53L3CX_RANGE_STATUS_RANGE_INVALID;
    }

    return ESP_OK;
}

esp_err_t stampfly_tof_wait_data_ready(stampfly_tof_handle_t *handle,
                                        stampfly_tof_sensor_t sensor,
                                        uint32_t timeout_ms)
{
    if (handle == NULL || !handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (sensor == STAMPFLY_TOF_SENSOR_FRONT) {
        return vl53l3cx_wait_data_ready(&handle->front_sensor, timeout_ms);
    } else if (sensor == STAMPFLY_TOF_SENSOR_BOTTOM) {
        return vl53l3cx_wait_data_ready(&handle->bottom_sensor, timeout_ms);
    } else {
        return ESP_ERR_INVALID_ARG;
    }
}

/**
 * @brief GPIO ISR handler for front sensor
 */
static void IRAM_ATTR stampfly_tof_front_isr_handler(void *arg)
{
    if (g_front_handle != NULL && g_front_handle->front_callback != NULL) {
        g_front_handle->front_callback(g_front_handle, STAMPFLY_TOF_SENSOR_FRONT);
    }
}

/**
 * @brief GPIO ISR handler for bottom sensor
 */
static void IRAM_ATTR stampfly_tof_bottom_isr_handler(void *arg)
{
    if (g_bottom_handle != NULL && g_bottom_handle->bottom_callback != NULL) {
        g_bottom_handle->bottom_callback(g_bottom_handle, STAMPFLY_TOF_SENSOR_BOTTOM);
    }
}

esp_err_t stampfly_tof_enable_interrupt(stampfly_tof_handle_t *handle,
                                         stampfly_tof_sensor_t sensor,
                                         stampfly_tof_interrupt_callback_t callback)
{
    if (handle == NULL || !handle->initialized || callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;

    // Install GPIO ISR service if not already installed
    static bool isr_service_installed = false;
    if (!isr_service_installed) {
        ret = gpio_install_isr_service(0);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to install ISR service");
            return ret;
        }
        isr_service_installed = true;
    }

    if (sensor == STAMPFLY_TOF_SENSOR_FRONT || sensor == STAMPFLY_TOF_SENSOR_BOTH) {
        // Configure front sensor interrupt
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << STAMPFLY_TOF_FRONT_INT_PIN),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_NEGEDGE,  // Interrupt on falling edge (ACTIVE_LOW)
        };
        ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure front INT pin for interrupt");
            return ret;
        }

        // Add ISR handler
        ret = gpio_isr_handler_add(STAMPFLY_TOF_FRONT_INT_PIN, stampfly_tof_front_isr_handler, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add front ISR handler");
            return ret;
        }

        handle->front_callback = callback;
        g_front_handle = handle;
        ESP_LOGI(TAG, "Front sensor interrupt enabled");
    }

    if (sensor == STAMPFLY_TOF_SENSOR_BOTTOM || sensor == STAMPFLY_TOF_SENSOR_BOTH) {
        // Configure bottom sensor interrupt
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << STAMPFLY_TOF_BOTTOM_INT_PIN),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_NEGEDGE,
        };
        ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure bottom INT pin for interrupt");
            return ret;
        }

        ret = gpio_isr_handler_add(STAMPFLY_TOF_BOTTOM_INT_PIN, stampfly_tof_bottom_isr_handler, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add bottom ISR handler");
            return ret;
        }

        handle->bottom_callback = callback;
        g_bottom_handle = handle;
        ESP_LOGI(TAG, "Bottom sensor interrupt enabled");
    }

    return ESP_OK;
}

esp_err_t stampfly_tof_disable_interrupt(stampfly_tof_handle_t *handle,
                                          stampfly_tof_sensor_t sensor)
{
    if (handle == NULL || !handle->initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;

    if (sensor == STAMPFLY_TOF_SENSOR_FRONT || sensor == STAMPFLY_TOF_SENSOR_BOTH) {
        ret = gpio_isr_handler_remove(STAMPFLY_TOF_FRONT_INT_PIN);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to remove front ISR handler");
        }

        // Disable interrupt on pin
        gpio_set_intr_type(STAMPFLY_TOF_FRONT_INT_PIN, GPIO_INTR_DISABLE);

        handle->front_callback = NULL;
        g_front_handle = NULL;
        ESP_LOGI(TAG, "Front sensor interrupt disabled");
    }

    if (sensor == STAMPFLY_TOF_SENSOR_BOTTOM || sensor == STAMPFLY_TOF_SENSOR_BOTH) {
        ret = gpio_isr_handler_remove(STAMPFLY_TOF_BOTTOM_INT_PIN);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to remove bottom ISR handler");
        }

        gpio_set_intr_type(STAMPFLY_TOF_BOTTOM_INT_PIN, GPIO_INTR_DISABLE);

        handle->bottom_callback = NULL;
        g_bottom_handle = NULL;
        ESP_LOGI(TAG, "Bottom sensor interrupt disabled");
    }

    return ESP_OK;
}
