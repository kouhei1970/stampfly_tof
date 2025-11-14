/**
 * @file stampfly_tof.h
 * @brief StampFly ToF Sensor Integration Layer
 *
 * This module provides high-level API for managing dual VL53L3CX sensors
 * on the M5StampFly platform.
 *
 * Hardware Configuration:
 * - Front ToF Sensor: XSHUT=GPIO9, INT=GPIO8
 * - Bottom ToF Sensor: XSHUT=GPIO7, INT=GPIO6
 * - I2C Bus: SDA=GPIO3, SCL=GPIO4
 */

#ifndef STAMPFLY_TOF_H
#define STAMPFLY_TOF_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "vl53l3cx.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief StampFly GPIO Pin Definitions
 */
// I2C Bus
#define STAMPFLY_TOF_I2C_SDA_PIN        GPIO_NUM_3
#define STAMPFLY_TOF_I2C_SCL_PIN        GPIO_NUM_4
#define STAMPFLY_TOF_I2C_FREQ_HZ        400000  // 400 kHz

// Front ToF Sensor
#define STAMPFLY_TOF_FRONT_XSHUT_PIN    GPIO_NUM_9
#define STAMPFLY_TOF_FRONT_INT_PIN      GPIO_NUM_8

// Bottom ToF Sensor
#define STAMPFLY_TOF_BOTTOM_XSHUT_PIN   GPIO_NUM_7
#define STAMPFLY_TOF_BOTTOM_INT_PIN     GPIO_NUM_6

/**
 * @brief I2C Addresses for Multi-Sensor Configuration
 */
#define STAMPFLY_TOF_FRONT_I2C_ADDR     0x30
#define STAMPFLY_TOF_BOTTOM_I2C_ADDR    0x31

/**
 * @brief Sensor Selection
 */
typedef enum {
    STAMPFLY_TOF_SENSOR_FRONT = 0,
    STAMPFLY_TOF_SENSOR_BOTTOM = 1,
    STAMPFLY_TOF_SENSOR_BOTH = 2
} stampfly_tof_sensor_t;

/**
 * @brief StampFly ToF System Handle
 */
typedef struct {
    vl53l3cx_dev_t front_sensor;    // Front sensor device
    vl53l3cx_dev_t bottom_sensor;   // Bottom sensor device
    i2c_port_t i2c_port;            // I2C port number
    bool initialized;               // Initialization state
} stampfly_tof_handle_t;

/**
 * @brief Dual Sensor Ranging Result
 */
typedef struct {
    uint16_t front_distance_mm;     // Front sensor distance (mm)
    uint8_t front_status;           // Front sensor status
    uint16_t bottom_distance_mm;    // Bottom sensor distance (mm)
    uint8_t bottom_status;          // Bottom sensor status
} stampfly_tof_dual_result_t;

/**
 * @brief Initialize StampFly ToF system
 *
 * This function performs the complete initialization sequence:
 * 1. Initialize I2C master
 * 2. Initialize GPIO pins (XSHUT, INT)
 * 3. Shutdown all sensors
 * 4. Bring up sensors one by one
 * 5. Change I2C addresses
 * 6. Initialize each sensor
 *
 * @param handle Pointer to system handle
 * @param i2c_port I2C port number to use (typically I2C_NUM_0)
 * @return ESP_OK on success
 */
esp_err_t stampfly_tof_init(stampfly_tof_handle_t *handle, i2c_port_t i2c_port);

/**
 * @brief Deinitialize StampFly ToF system
 *
 * @param handle Pointer to system handle
 * @return ESP_OK on success
 */
esp_err_t stampfly_tof_deinit(stampfly_tof_handle_t *handle);

/**
 * @brief Start ranging on selected sensor(s)
 *
 * @param handle Pointer to system handle
 * @param sensor Sensor selection
 * @return ESP_OK on success
 */
esp_err_t stampfly_tof_start_ranging(stampfly_tof_handle_t *handle, stampfly_tof_sensor_t sensor);

/**
 * @brief Stop ranging on selected sensor(s)
 *
 * @param handle Pointer to system handle
 * @param sensor Sensor selection
 * @return ESP_OK on success
 */
esp_err_t stampfly_tof_stop_ranging(stampfly_tof_handle_t *handle, stampfly_tof_sensor_t sensor);

/**
 * @brief Get distance from front sensor
 *
 * @param handle Pointer to system handle
 * @param result Pointer to result structure
 * @return ESP_OK on success
 */
esp_err_t stampfly_tof_get_front_distance(stampfly_tof_handle_t *handle, vl53l3cx_result_t *result);

/**
 * @brief Get distance from bottom sensor
 *
 * @param handle Pointer to system handle
 * @param result Pointer to result structure
 * @return ESP_OK on success
 */
esp_err_t stampfly_tof_get_bottom_distance(stampfly_tof_handle_t *handle, vl53l3cx_result_t *result);

/**
 * @brief Get distance from both sensors
 *
 * @param handle Pointer to system handle
 * @param result Pointer to dual result structure
 * @return ESP_OK on success
 */
esp_err_t stampfly_tof_get_dual_distance(stampfly_tof_handle_t *handle, stampfly_tof_dual_result_t *result);

/**
 * @brief Wait for data ready on selected sensor
 *
 * @param handle Pointer to system handle
 * @param sensor Sensor selection
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t stampfly_tof_wait_data_ready(stampfly_tof_handle_t *handle,
                                        stampfly_tof_sensor_t sensor,
                                        uint32_t timeout_ms);

/**
 * @brief Control XSHUT pin (shutdown/wakeup sensor)
 *
 * @param sensor Sensor selection
 * @param level 1 = enable sensor, 0 = shutdown sensor
 * @return ESP_OK on success
 */
esp_err_t stampfly_tof_set_xshut(stampfly_tof_sensor_t sensor, uint8_t level);

/**
 * @brief Read INT pin state
 *
 * @param sensor Sensor selection
 * @param level Pointer to store pin level
 * @return ESP_OK on success
 */
esp_err_t stampfly_tof_get_int_pin(stampfly_tof_sensor_t sensor, uint8_t *level);

#ifdef __cplusplus
}
#endif

#endif // STAMPFLY_TOF_H
