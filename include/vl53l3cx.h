/**
 * @file vl53l3cx.h
 * @brief VL53L3CX Time-of-Flight Distance Sensor Driver for ESP32
 *
 * This driver provides a complete implementation for the VL53L3CX ToF sensor
 * based on register-level control as documented in the VL53L3CX datasheet.
 *
 * Features:
 * - Firmware boot sequence
 * - NVM calibration data readout
 * - MEDIUM_RANGE preset mode
 * - Continuous ranging mode
 * - Histogram-based distance calculation
 * - Multi-sensor support via I2C address change
 *
 * @note This driver is designed for ESP-IDF on ESP32-S3
 */

#ifndef VL53L3CX_H
#define VL53L3CX_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief VL53L3CX Register Addresses
 */
// Boot and System Registers
#define VL53L3CX_REG_SOFT_RESET                         0x0000
#define VL53L3CX_REG_I2C_SLAVE_DEVICE_ADDRESS           0x0001
#define VL53L3CX_REG_FIRMWARE_SYSTEM_STATUS             0x0010
#define VL53L3CX_REG_PAD_I2C_HV_EXTSUP_CONFIG           0x002E

// GPIO and Interrupt Configuration
#define VL53L3CX_REG_GPIO_HV_MUX_CTRL                   0x0030
#define VL53L3CX_REG_GPIO_TIO_HV_STATUS                 0x0031
#define VL53L3CX_REG_SYSTEM_INTERRUPT_CONFIG_GPIO       0x0046
#define VL53L3CX_REG_SYSTEM_INTERRUPT_CLEAR             0x0086
#define VL53L3CX_REG_SYSTEM_MODE_START                  0x0087
#define VL53L3CX_REG_RESULT_INTERRUPT_STATUS            0x0089

// Static Configuration
#define VL53L3CX_REG_ANA_CONFIG_SPAD_SEL_PSWIDTH        0x0033
#define VL53L3CX_REG_ANA_CONFIG_VCSEL_PULSE_WIDTH_OFFSET 0x0034
#define VL53L3CX_REG_SIGMA_ESTIMATOR_EFFECTIVE_PULSE_WIDTH_NS 0x0036
#define VL53L3CX_REG_SIGMA_ESTIMATOR_EFFECTIVE_AMBIENT_WIDTH_NS 0x0037
#define VL53L3CX_REG_SIGMA_ESTIMATOR_SIGMA_REF_MM       0x0038
#define VL53L3CX_REG_ALGO_CROSSTALK_COMPENSATION_VALID_HEIGHT_MM 0x0039
#define VL53L3CX_REG_ALGO_RANGE_IGNORE_VALID_HEIGHT_MM  0x003E
#define VL53L3CX_REG_ALGO_RANGE_MIN_CLIP                0x003F
#define VL53L3CX_REG_ALGO_CONSISTENCY_CHECK_TOLERANCE   0x0040

// General Configuration
#define VL53L3CX_REG_CAL_CONFIG_VCSEL_START             0x0047
#define VL53L3CX_REG_CAL_CONFIG_REPEAT_RATE             0x0048
#define VL53L3CX_REG_GLOBAL_CONFIG_VCSEL_WIDTH          0x004A
#define VL53L3CX_REG_PHASECAL_CONFIG_TIMEOUT_MACROP     0x004B
#define VL53L3CX_REG_PHASECAL_CONFIG_TARGET             0x004C

// Timing Configuration
#define VL53L3CX_REG_MM_CONFIG_TIMEOUT_MACROP_A         0x005A
#define VL53L3CX_REG_MM_CONFIG_TIMEOUT_MACROP_B         0x005C
#define VL53L3CX_REG_RANGE_CONFIG_TIMEOUT_MACROP_A      0x005E
#define VL53L3CX_REG_RANGE_CONFIG_VCSEL_PERIOD_A        0x0060
#define VL53L3CX_REG_RANGE_CONFIG_TIMEOUT_MACROP_B      0x0061
#define VL53L3CX_REG_RANGE_CONFIG_VCSEL_PERIOD_B        0x0063
#define VL53L3CX_REG_SYSTEM_INTERMEASUREMENT_PERIOD     0x006C

// Dynamic Configuration
#define VL53L3CX_REG_SYSTEM_GROUPED_PARAMETER_HOLD_0    0x0071
#define VL53L3CX_REG_SYSTEM_THRESH_HIGH                 0x0072
#define VL53L3CX_REG_SYSTEM_THRESH_LOW                  0x0074
#define VL53L3CX_REG_SYSTEM_SEED_CONFIG                 0x0077
#define VL53L3CX_REG_SD_CONFIG_WOI_SD0                  0x0078
#define VL53L3CX_REG_SD_CONFIG_WOI_SD1                  0x0079
#define VL53L3CX_REG_SD_CONFIG_INITIAL_PHASE_SD0        0x007A
#define VL53L3CX_REG_SD_CONFIG_INITIAL_PHASE_SD1        0x007B
#define VL53L3CX_REG_SYSTEM_GROUPED_PARAMETER_HOLD_1    0x007C
#define VL53L3CX_REG_ROI_CONFIG_USER_ROI_CENTRE_SPAD    0x007F
#define VL53L3CX_REG_ROI_CONFIG_USER_ROI_REQUESTED_GLOBAL_XY_SIZE 0x0080
#define VL53L3CX_REG_SYSTEM_SEQUENCE_CONFIG             0x0081
#define VL53L3CX_REG_SYSTEM_GROUPED_PARAMETER_HOLD      0x0082

// System Control
#define VL53L3CX_REG_SYSTEM_STREAM_COUNT_CTRL           0x0083
#define VL53L3CX_REG_FIRMWARE_ENABLE                    0x0401
#define VL53L3CX_REG_POWER_MANAGEMENT_GO1_POWER_FORCE   0x0419

// NVM Control
#define VL53L3CX_REG_RANGING_CORE_NVM_CTRL_PDN          0x01AC
#define VL53L3CX_REG_RANGING_CORE_NVM_CTRL_MODE         0x01AD
#define VL53L3CX_REG_RANGING_CORE_NVM_CTRL_PULSE_WIDTH_MSB 0x01AE
#define VL53L3CX_REG_RANGING_CORE_NVM_CTRL_ADDR         0x01B0
#define VL53L3CX_REG_RANGING_CORE_NVM_CTRL_READN        0x01B1
#define VL53L3CX_REG_RANGING_CORE_NVM_CTRL_DATAOUT_MMM  0x01B2
#define VL53L3CX_REG_RANGING_CORE_CLK_CTRL1             0x01BB

/**
 * @brief Default I2C Address (7-bit)
 */
#define VL53L3CX_DEFAULT_I2C_ADDR   0x29

/**
 * @brief Range Status Codes
 */
#define VL53L3CX_RANGE_STATUS_RANGE_VALID               0x09
#define VL53L3CX_RANGE_STATUS_SIGMA_FAIL                0x01
#define VL53L3CX_RANGE_STATUS_SIGNAL_FAIL               0x02
#define VL53L3CX_RANGE_STATUS_RANGE_VALID_MIN_RANGE_CLIPPED 0x0B
#define VL53L3CX_RANGE_STATUS_OUTOFBOUNDS_FAIL          0x04
#define VL53L3CX_RANGE_STATUS_HARDWARE_FAIL             0x05
#define VL53L3CX_RANGE_STATUS_RANGE_VALID_NO_WRAP_CHECK_FAIL 0x06
#define VL53L3CX_RANGE_STATUS_WRAP_TARGET_FAIL          0x07
#define VL53L3CX_RANGE_STATUS_PROCESSING_FAIL           0x08
#define VL53L3CX_RANGE_STATUS_XTALK_SIGNAL_FAIL         0x0A
#define VL53L3CX_RANGE_STATUS_SYNCRONISATION_INT        0x0C
#define VL53L3CX_RANGE_STATUS_RANGE_VALID_MERGED_PULSE  0x0D
#define VL53L3CX_RANGE_STATUS_TARGET_PRESENT_LACK_OF_SIGNAL 0x0E
#define VL53L3CX_RANGE_STATUS_MIN_RANGE_FAIL            0x0F
#define VL53L3CX_RANGE_STATUS_RANGE_INVALID             0x11

/**
 * @brief Mode Start Commands
 */
#define VL53L3CX_MODE_START_STOP                        0x00
#define VL53L3CX_MODE_START_BACKTOBACK                  0x42  // Continuous ranging
#define VL53L3CX_MODE_START_SINGLESHOT                  0x12  // Single shot

/**
 * @brief Histogram Bins Configuration
 */
#define VL53L3CX_HISTOGRAM_BINS                         24
#define VL53L3CX_HISTOGRAM_HEADER_SIZE                  5
#define VL53L3CX_HISTOGRAM_TOTAL_SIZE                   77  // 5 + 24*3

/**
 * @brief Timing Constants
 */
#define VL53L3CX_BOOT_TIMEOUT_MS                        500
#define VL53L3CX_RANGING_TIMEOUT_MS                     2000
#define VL53L3CX_POLL_INTERVAL_MS                       1

/**
 * @brief Device Structure
 */
typedef struct {
    uint8_t i2c_addr;                   // I2C address (7-bit)
    i2c_master_bus_handle_t i2c_bus;    // I2C bus handle
    i2c_master_dev_handle_t i2c_dev;    // I2C device handle
    bool measurement_active;            // Measurement state flag
} vl53l3cx_dev_t;

/**
 * @brief Ranging Result Structure
 */
typedef struct {
    uint16_t distance_mm;               // Measured distance in mm
    uint8_t range_status;               // Range status code
    uint8_t stream_count;               // Stream counter
    uint32_t bin_data[VL53L3CX_HISTOGRAM_BINS];  // Raw histogram data
    uint32_t ambient_estimate;          // Estimated ambient level
    uint8_t peak_bin;                   // Peak bin index
} vl53l3cx_result_t;

/**
 * @brief Initialize I2C master bus for VL53L3CX communication
 *
 * @param bus_handle Pointer to store the I2C bus handle
 * @param i2c_port I2C port number
 * @param sda_io SDA GPIO number
 * @param scl_io SCL GPIO number
 * @param clk_speed I2C clock speed in Hz (typically 400000)
 * @return ESP_OK on success
 */
esp_err_t vl53l3cx_i2c_master_init(i2c_master_bus_handle_t *bus_handle, i2c_port_t i2c_port,
                                   int sda_io, int scl_io, uint32_t clk_speed);

/**
 * @brief Deinitialize I2C master bus
 *
 * @param bus_handle I2C bus handle
 * @return ESP_OK on success
 */
esp_err_t vl53l3cx_i2c_master_deinit(i2c_master_bus_handle_t bus_handle);

/**
 * @brief Wait for firmware boot completion
 *
 * @param dev Device handle
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t vl53l3cx_wait_boot(vl53l3cx_dev_t *dev);

/**
 * @brief Set MEDIUM_RANGE preset mode
 *
 * @param dev Device handle
 * @return ESP_OK on success
 */
esp_err_t vl53l3cx_set_preset_mode_medium_range(vl53l3cx_dev_t *dev);

/**
 * @brief Initialize VL53L3CX device
 *
 * This function performs the complete initialization sequence:
 * 1. Wait for firmware boot
 * 2. Configure I2C voltage (if needed)
 * 3. Read NVM calibration data
 * 4. Set MEDIUM_RANGE preset mode
 *
 * @param dev Device handle
 * @param bus_handle I2C bus handle
 * @param i2c_addr I2C address (7-bit), use VL53L3CX_DEFAULT_I2C_ADDR for default
 * @return ESP_OK on success
 */
esp_err_t vl53l3cx_init(vl53l3cx_dev_t *dev, i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr);

/**
 * @brief Change I2C device address
 *
 * @note Address change is volatile and resets to default (0x29) on power cycle
 *
 * @param dev Device handle
 * @param new_addr New I2C address (7-bit), valid range: 0x08-0x77
 * @return ESP_OK on success
 */
esp_err_t vl53l3cx_set_device_address(vl53l3cx_dev_t *dev, uint8_t new_addr);

/**
 * @brief Start continuous ranging mode
 *
 * @param dev Device handle
 * @return ESP_OK on success
 */
esp_err_t vl53l3cx_start_ranging(vl53l3cx_dev_t *dev);

/**
 * @brief Stop ranging
 *
 * @param dev Device handle
 * @return ESP_OK on success
 */
esp_err_t vl53l3cx_stop_ranging(vl53l3cx_dev_t *dev);

/**
 * @brief Wait for measurement data ready (polling method)
 *
 * @param dev Device handle
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t vl53l3cx_wait_data_ready(vl53l3cx_dev_t *dev, uint32_t timeout_ms);

/**
 * @brief Get ranging measurement data
 *
 * This function reads histogram data, performs ambient removal,
 * peak detection, and distance calculation.
 *
 * @param dev Device handle
 * @param result Pointer to result structure
 * @return ESP_OK on success
 */
esp_err_t vl53l3cx_get_ranging_data(vl53l3cx_dev_t *dev, vl53l3cx_result_t *result);

/**
 * @brief Clear interrupt
 *
 * @param dev Device handle
 * @return ESP_OK on success
 */
esp_err_t vl53l3cx_clear_interrupt(vl53l3cx_dev_t *dev);

/**
 * @brief Get range status string
 *
 * @param status Range status code
 * @return Human-readable status string
 */
const char* vl53l3cx_get_range_status_string(uint8_t status);

/**
 * @brief Check if data is ready (non-blocking)
 *
 * @param dev Device handle
 * @param ready Pointer to store ready flag (1=ready, 0=not ready)
 * @return ESP_OK on success
 */
esp_err_t vl53l3cx_check_data_ready(vl53l3cx_dev_t *dev, uint8_t *ready);

/**
 * @brief Data ready callback function type
 *
 * @param dev Device handle that triggered the interrupt
 */
typedef void (*vl53l3cx_data_ready_callback_t)(vl53l3cx_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif // VL53L3CX_H
