/**
 * @file main.c
 * @brief StampFly ToF Basic Example
 *
 * This example demonstrates basic usage of the VL53L3CX ToF sensors
 * on M5StampFly platform.
 *
 * Features:
 * - Initialize dual ToF sensors (front and bottom)
 * - Continuous ranging mode
 * - Display distance measurements on serial console
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "stampfly_tof.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "StampFly ToF Basic Example");
    ESP_LOGI(TAG, "=========================");

    // Initialize ToF system
    stampfly_tof_handle_t tof_handle;
    esp_err_t ret = stampfly_tof_init(&tof_handle, I2C_NUM_0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ToF initialization failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "ToF system initialized successfully");

    // Start ranging on both sensors
    ret = stampfly_tof_start_ranging(&tof_handle, STAMPFLY_TOF_SENSOR_BOTH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ranging: %s", esp_err_to_name(ret));
        stampfly_tof_deinit(&tof_handle);
        return;
    }

    ESP_LOGI(TAG, "Ranging started on both sensors");
    ESP_LOGI(TAG, "Starting measurement loop...");
    ESP_LOGI(TAG, "");

    // Main measurement loop
    while (1) {
        stampfly_tof_dual_result_t result;

        // Get distance from both sensors
        ret = stampfly_tof_get_dual_distance(&tof_handle, &result);
        if (ret == ESP_OK) {
            // Print results
            printf("Front: ");
            if (result.front_status == VL53L3CX_RANGE_STATUS_RANGE_VALID) {
                printf("%4d mm  ", result.front_distance_mm);
            } else {
                printf("  --  mm  ");
            }
            printf("[%s]", vl53l3cx_get_range_status_string(result.front_status));

            printf("  |  ");

            printf("Bottom: ");
            if (result.bottom_status == VL53L3CX_RANGE_STATUS_RANGE_VALID) {
                printf("%4d mm  ", result.bottom_distance_mm);
            } else {
                printf("  --  mm  ");
            }
            printf("[%s]", vl53l3cx_get_range_status_string(result.bottom_status));

            printf("\n");
        } else {
            ESP_LOGW(TAG, "Failed to get distance: %s", esp_err_to_name(ret));
        }

        // Wait before next measurement (adjust as needed)
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // Cleanup (unreachable in this example)
    stampfly_tof_stop_ranging(&tof_handle, STAMPFLY_TOF_SENSOR_BOTH);
    stampfly_tof_deinit(&tof_handle);
}
