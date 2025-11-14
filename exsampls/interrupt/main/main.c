/**
 * @file main.c
 * @brief StampFly ToF Interrupt Mode Example
 *
 * This example demonstrates interrupt-driven ranging using GPIO INT pins.
 * This method is more efficient than polling for real-time applications.
 *
 * Features:
 * - Initialize dual ToF sensors
 * - Enable GPIO interrupts on INT pins
 * - Use FreeRTOS semaphores for synchronization
 * - Display measurements when data is ready
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "stampfly_tof.h"

static const char *TAG = "MAIN";

// Semaphores for interrupt synchronization
static SemaphoreHandle_t front_data_ready_sem = NULL;
static SemaphoreHandle_t bottom_data_ready_sem = NULL;

/**
 * @brief Interrupt callback for data ready
 *
 * This function is called from ISR when new ranging data is available.
 * It gives a semaphore to wake up the corresponding task.
 */
static void IRAM_ATTR data_ready_callback(stampfly_tof_handle_t *handle, stampfly_tof_sensor_t sensor)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (sensor == STAMPFLY_TOF_SENSOR_FRONT) {
        xSemaphoreGiveFromISR(front_data_ready_sem, &xHigherPriorityTaskWoken);
    } else if (sensor == STAMPFLY_TOF_SENSOR_BOTTOM) {
        xSemaphoreGiveFromISR(bottom_data_ready_sem, &xHigherPriorityTaskWoken);
    }

    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief Task for front sensor data processing
 */
static void front_sensor_task(void *arg)
{
    stampfly_tof_handle_t *tof = (stampfly_tof_handle_t *)arg;
    vl53l3cx_result_t result;

    ESP_LOGI(TAG, "Front sensor task started");

    while (1) {
        // Wait for interrupt (data ready)
        if (xSemaphoreTake(front_data_ready_sem, portMAX_DELAY) == pdTRUE) {
            // Get ranging data (no polling needed - data is already ready)
            uint8_t ready;
            if (vl53l3cx_check_data_ready(&tof->front_sensor, &ready) == ESP_OK && ready) {
                if (vl53l3cx_get_ranging_data(&tof->front_sensor, &result) == ESP_OK) {
                    if (result.range_status == VL53L3CX_RANGE_STATUS_RANGE_VALID) {
                        printf("[FRONT] Distance: %4d mm | Status: %s\n",
                               result.distance_mm,
                               vl53l3cx_get_range_status_string(result.range_status));
                    } else {
                        printf("[FRONT] Error: %s\n",
                               vl53l3cx_get_range_status_string(result.range_status));
                    }
                }
            }
        }
    }
}

/**
 * @brief Task for bottom sensor data processing
 */
static void bottom_sensor_task(void *arg)
{
    stampfly_tof_handle_t *tof = (stampfly_tof_handle_t *)arg;
    vl53l3cx_result_t result;

    ESP_LOGI(TAG, "Bottom sensor task started");

    while (1) {
        // Wait for interrupt (data ready)
        if (xSemaphoreTake(bottom_data_ready_sem, portMAX_DELAY) == pdTRUE) {
            // Get ranging data
            uint8_t ready;
            if (vl53l3cx_check_data_ready(&tof->bottom_sensor, &ready) == ESP_OK && ready) {
                if (vl53l3cx_get_ranging_data(&tof->bottom_sensor, &result) == ESP_OK) {
                    if (result.range_status == VL53L3CX_RANGE_STATUS_RANGE_VALID) {
                        printf("[BOTTOM] Distance: %4d mm | Status: %s\n",
                               result.distance_mm,
                               vl53l3cx_get_range_status_string(result.range_status));
                    } else {
                        printf("[BOTTOM] Error: %s\n",
                               vl53l3cx_get_range_status_string(result.range_status));
                    }
                }
            }
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "StampFly ToF Interrupt Mode Example");
    ESP_LOGI(TAG, "====================================");

    // Create semaphores
    front_data_ready_sem = xSemaphoreCreateBinary();
    bottom_data_ready_sem = xSemaphoreCreateBinary();

    if (front_data_ready_sem == NULL || bottom_data_ready_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create semaphores");
        return;
    }

    // Initialize ToF system
    stampfly_tof_handle_t tof_handle;
    esp_err_t ret = stampfly_tof_init(&tof_handle, I2C_NUM_0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ToF initialization failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "ToF system initialized successfully");

    // Enable GPIO interrupts
    ret = stampfly_tof_enable_interrupt(&tof_handle,
                                        STAMPFLY_TOF_SENSOR_BOTH,
                                        data_ready_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable interrupts: %s", esp_err_to_name(ret));
        stampfly_tof_deinit(&tof_handle);
        return;
    }

    ESP_LOGI(TAG, "GPIO interrupts enabled");

    // Start ranging on both sensors
    ret = stampfly_tof_start_ranging(&tof_handle, STAMPFLY_TOF_SENSOR_BOTH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ranging: %s", esp_err_to_name(ret));
        stampfly_tof_disable_interrupt(&tof_handle, STAMPFLY_TOF_SENSOR_BOTH);
        stampfly_tof_deinit(&tof_handle);
        return;
    }

    ESP_LOGI(TAG, "Ranging started on both sensors");
    ESP_LOGI(TAG, "Waiting for interrupts...");
    ESP_LOGI(TAG, "");

    // Create tasks for each sensor
    xTaskCreate(front_sensor_task, "front_tof", 4096, &tof_handle, 5, NULL);
    xTaskCreate(bottom_sensor_task, "bottom_tof", 4096, &tof_handle, 5, NULL);

    // Main loop (monitoring)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));  // Print status every 5 seconds
        ESP_LOGI(TAG, "System running... (interrupt-driven mode)");
    }

    // Cleanup (unreachable in this example)
    stampfly_tof_disable_interrupt(&tof_handle, STAMPFLY_TOF_SENSOR_BOTH);
    stampfly_tof_stop_ranging(&tof_handle, STAMPFLY_TOF_SENSOR_BOTH);
    stampfly_tof_deinit(&tof_handle);
}
