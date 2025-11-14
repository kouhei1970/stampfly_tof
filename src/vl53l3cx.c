/**
 * @file vl53l3cx.c
 * @brief VL53L3CX Time-of-Flight Distance Sensor Driver Implementation
 *
 * Complete register-level implementation based on VL53L3CX documentation.
 */

#include "vl53l3cx.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "VL53L3CX";

// Timing macros
#define DELAY_MS(ms)  vTaskDelay(pdMS_TO_TICKS(ms))
#define DELAY_US(us)  esp_rom_delay_us(us)
#define MILLIS()      (xTaskGetTickCount() * portTICK_PERIOD_MS)

/**
 * @brief I2C write helper function
 */
static esp_err_t vl53l3cx_write_reg(vl53l3cx_dev_t *dev, uint16_t reg_addr,
                                     const uint8_t *data, size_t len)
{
    if (dev == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t write_buf[len + 2];
    write_buf[0] = (reg_addr >> 8) & 0xFF;  // MSB
    write_buf[1] = reg_addr & 0xFF;         // LSB
    memcpy(&write_buf[2], data, len);

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, write_buf, len + 2, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(dev->i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    return ret;
}

/**
 * @brief I2C read helper function
 */
static esp_err_t vl53l3cx_read_reg(vl53l3cx_dev_t *dev, uint16_t reg_addr,
                                    uint8_t *data, size_t len)
{
    if (dev == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t addr_buf[2];
    addr_buf[0] = (reg_addr >> 8) & 0xFF;
    addr_buf[1] = reg_addr & 0xFF;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, addr_buf, 2, true);
    i2c_master_start(cmd);  // Repeated start
    i2c_master_write_byte(cmd, (dev->i2c_addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(dev->i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    return ret;
}

/**
 * @brief Write single byte helper
 */
static esp_err_t vl53l3cx_write_byte(vl53l3cx_dev_t *dev, uint16_t reg_addr, uint8_t value)
{
    return vl53l3cx_write_reg(dev, reg_addr, &value, 1);
}

/**
 * @brief Write 16-bit value (big-endian)
 */
static esp_err_t vl53l3cx_write_word(vl53l3cx_dev_t *dev, uint16_t reg_addr, uint16_t value)
{
    uint8_t buf[2];
    buf[0] = (value >> 8) & 0xFF;
    buf[1] = value & 0xFF;
    return vl53l3cx_write_reg(dev, reg_addr, buf, 2);
}

/**
 * @brief Write 32-bit value (big-endian)
 */
static esp_err_t vl53l3cx_write_dword(vl53l3cx_dev_t *dev, uint16_t reg_addr, uint32_t value)
{
    uint8_t buf[4];
    buf[0] = (value >> 24) & 0xFF;
    buf[1] = (value >> 16) & 0xFF;
    buf[2] = (value >> 8) & 0xFF;
    buf[3] = value & 0xFF;
    return vl53l3cx_write_reg(dev, reg_addr, buf, 4);
}

/**
 * @brief Read single byte helper
 */
static esp_err_t vl53l3cx_read_byte(vl53l3cx_dev_t *dev, uint16_t reg_addr, uint8_t *value)
{
    return vl53l3cx_read_reg(dev, reg_addr, value, 1);
}

// ============================================================================
// Public API Implementation
// ============================================================================

esp_err_t vl53l3cx_i2c_master_init(i2c_port_t i2c_port, int sda_io, int scl_io, uint32_t clk_speed)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_io,
        .scl_io_num = scl_io,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = clk_speed,
    };

    esp_err_t ret = i2c_param_config(i2c_port, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed");
        return ret;
    }

    ret = i2c_driver_install(i2c_port, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed");
        return ret;
    }

    ESP_LOGI(TAG, "I2C master initialized on port %d (SDA=%d, SCL=%d, %lu Hz)",
             i2c_port, sda_io, scl_io, clk_speed);
    return ESP_OK;
}

esp_err_t vl53l3cx_i2c_master_deinit(i2c_port_t i2c_port)
{
    return i2c_driver_delete(i2c_port);
}

esp_err_t vl53l3cx_wait_boot(vl53l3cx_dev_t *dev)
{
    uint8_t boot_status;
    uint32_t start_time = MILLIS();
    uint32_t timeout = VL53L3CX_BOOT_TIMEOUT_MS;

    ESP_LOGI(TAG, "Waiting for firmware boot...");

    do {
        esp_err_t ret = vl53l3cx_read_byte(dev, VL53L3CX_REG_FIRMWARE_SYSTEM_STATUS, &boot_status);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read boot status");
            return ret;
        }

        if ((MILLIS() - start_time) > timeout) {
            ESP_LOGE(TAG, "Boot timeout");
            return ESP_ERR_TIMEOUT;
        }

        DELAY_MS(1);

    } while ((boot_status & 0x01) == 0);

    ESP_LOGI(TAG, "Firmware boot complete");
    return ESP_OK;
}

esp_err_t vl53l3cx_read_nvm_calibration_data(vl53l3cx_dev_t *dev)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Reading NVM calibration data...");

    // Step 1: Disable firmware
    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_FIRMWARE_ENABLE, 0x00);
    if (ret != ESP_OK) return ret;

    // Step 2: Enable power force
    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_POWER_MANAGEMENT_GO1_POWER_FORCE, 0x01);
    if (ret != ESP_OK) return ret;
    DELAY_US(250);  // Power stabilization

    // Step 3: NVM power down release
    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_RANGING_CORE_NVM_CTRL_PDN, 0x01);
    if (ret != ESP_OK) return ret;

    // Step 4: Enable NVM clock
    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_RANGING_CORE_CLK_CTRL1, 0x05);
    if (ret != ESP_OK) return ret;
    DELAY_US(5000);  // NVM power-up wait (5ms)

    // Step 5: Set NVM mode to read
    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_RANGING_CORE_NVM_CTRL_MODE, 0x01);
    if (ret != ESP_OK) return ret;

    // Step 6: Set NVM pulse width
    ret = vl53l3cx_write_word(dev, VL53L3CX_REG_RANGING_CORE_NVM_CTRL_PULSE_WIDTH_MSB, 0x0004);
    if (ret != ESP_OK) return ret;

    // Step 7: Read fast oscillator frequency (NVM address 0x1C)
    uint8_t nvm_addr = 0x1C;
    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_RANGING_CORE_NVM_CTRL_ADDR, nvm_addr);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_RANGING_CORE_NVM_CTRL_READN, 0x00);
    if (ret != ESP_OK) return ret;
    DELAY_US(5);  // Read trigger delay

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_RANGING_CORE_NVM_CTRL_READN, 0x01);
    if (ret != ESP_OK) return ret;

    uint8_t nvm_data[4];
    ret = vl53l3cx_read_reg(dev, VL53L3CX_REG_RANGING_CORE_NVM_CTRL_DATAOUT_MMM, nvm_data, 4);
    if (ret != ESP_OK) return ret;

    // Extract 16-bit fast oscillator frequency (big-endian)
    dev->fast_osc_frequency = ((uint16_t)nvm_data[0] << 8) | nvm_data[1];

    // Step 8: Cleanup - disable power force
    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_POWER_MANAGEMENT_GO1_POWER_FORCE, 0x00);
    if (ret != ESP_OK) return ret;

    // Step 9: Re-enable firmware
    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_FIRMWARE_ENABLE, 0x01);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "NVM calibration data read complete (OSC freq: %d)", dev->fast_osc_frequency);
    return ESP_OK;
}

esp_err_t vl53l3cx_set_preset_mode_medium_range(vl53l3cx_dev_t *dev)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Setting MEDIUM_RANGE preset mode...");

    // ========================================
    // Static Configuration
    // ========================================
    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_GPIO_HV_MUX_CTRL, 0x10);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_GPIO_TIO_HV_STATUS, 0x02);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_ANA_CONFIG_SPAD_SEL_PSWIDTH, 0x02);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_ANA_CONFIG_VCSEL_PULSE_WIDTH_OFFSET, 0x08);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SIGMA_ESTIMATOR_EFFECTIVE_PULSE_WIDTH_NS, 0x08);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SIGMA_ESTIMATOR_EFFECTIVE_AMBIENT_WIDTH_NS, 0x10);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SIGMA_ESTIMATOR_SIGMA_REF_MM, 0x01);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_ALGO_CROSSTALK_COMPENSATION_VALID_HEIGHT_MM, 0x01);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_ALGO_RANGE_IGNORE_VALID_HEIGHT_MM, 0xFF);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_ALGO_RANGE_MIN_CLIP, 0x00);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_ALGO_CONSISTENCY_CHECK_TOLERANCE, 0x02);
    if (ret != ESP_OK) return ret;

    // ========================================
    // General Configuration
    // ========================================
    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SYSTEM_INTERRUPT_CONFIG_GPIO, 0x20);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_CAL_CONFIG_VCSEL_START, 0x0B);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_word(dev, VL53L3CX_REG_CAL_CONFIG_REPEAT_RATE, 0x0000);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_GLOBAL_CONFIG_VCSEL_WIDTH, 0x02);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_PHASECAL_CONFIG_TIMEOUT_MACROP, 0x0D);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_PHASECAL_CONFIG_TARGET, 0x21);
    if (ret != ESP_OK) return ret;

    // ========================================
    // Timing Configuration
    // ========================================
    ret = vl53l3cx_write_word(dev, VL53L3CX_REG_MM_CONFIG_TIMEOUT_MACROP_A, 0x001A);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_word(dev, VL53L3CX_REG_MM_CONFIG_TIMEOUT_MACROP_B, 0x0020);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_word(dev, VL53L3CX_REG_RANGE_CONFIG_TIMEOUT_MACROP_A, 0x01CC);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_RANGE_CONFIG_VCSEL_PERIOD_A, 0x0B);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_word(dev, VL53L3CX_REG_RANGE_CONFIG_TIMEOUT_MACROP_B, 0x01F5);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_RANGE_CONFIG_VCSEL_PERIOD_B, 0x09);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_dword(dev, VL53L3CX_REG_SYSTEM_INTERMEASUREMENT_PERIOD, 100);
    if (ret != ESP_OK) return ret;

    // ========================================
    // Dynamic Configuration
    // ========================================
    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SYSTEM_GROUPED_PARAMETER_HOLD_0, 0x01);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_word(dev, VL53L3CX_REG_SYSTEM_THRESH_HIGH, 0x0000);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_word(dev, VL53L3CX_REG_SYSTEM_THRESH_LOW, 0x0000);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SYSTEM_SEED_CONFIG, 0x02);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SD_CONFIG_WOI_SD0, 0x0B);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SD_CONFIG_WOI_SD1, 0x09);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SD_CONFIG_INITIAL_PHASE_SD0, 0x0A);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SD_CONFIG_INITIAL_PHASE_SD1, 0x0A);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SYSTEM_GROUPED_PARAMETER_HOLD_1, 0x01);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_ROI_CONFIG_USER_ROI_CENTRE_SPAD, 0xC7);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_ROI_CONFIG_USER_ROI_REQUESTED_GLOBAL_XY_SIZE, 0xFF);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SYSTEM_SEQUENCE_CONFIG, 0xC1);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SYSTEM_GROUPED_PARAMETER_HOLD, 0x02);
    if (ret != ESP_OK) return ret;

    // ========================================
    // System Control
    // ========================================
    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SYSTEM_STREAM_COUNT_CTRL, 0x00);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_FIRMWARE_ENABLE, 0x01);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SYSTEM_INTERRUPT_CLEAR, 0x01);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "MEDIUM_RANGE preset mode configured");
    return ESP_OK;
}

esp_err_t vl53l3cx_init(vl53l3cx_dev_t *dev, i2c_port_t i2c_port, uint8_t i2c_addr)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    dev->i2c_port = i2c_port;
    dev->i2c_addr = i2c_addr;
    dev->measurement_active = false;

    ESP_LOGI(TAG, "Initializing VL53L3CX at address 0x%02X", i2c_addr);

    // Step 1: Wait for firmware boot
    esp_err_t ret = vl53l3cx_wait_boot(dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Firmware boot failed");
        return ret;
    }

    // Step 2: Read NVM calibration data
    ret = vl53l3cx_read_nvm_calibration_data(dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVM calibration read failed");
        return ret;
    }

    // Step 3: Set MEDIUM_RANGE preset mode
    ret = vl53l3cx_set_preset_mode_medium_range(dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Preset mode configuration failed");
        return ret;
    }

    ESP_LOGI(TAG, "VL53L3CX initialization complete");
    return ESP_OK;
}

esp_err_t vl53l3cx_set_device_address(vl53l3cx_dev_t *dev, uint8_t new_addr)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (new_addr < 0x08 || new_addr > 0x77) {
        ESP_LOGE(TAG, "Invalid I2C address: 0x%02X", new_addr);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Changing I2C address: 0x%02X -> 0x%02X", dev->i2c_addr, new_addr);

    uint8_t addr_value = new_addr & 0x7F;
    esp_err_t ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_I2C_SLAVE_DEVICE_ADDRESS, addr_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write new I2C address");
        return ret;
    }

    // Update device handle with new address
    dev->i2c_addr = new_addr;

    ESP_LOGI(TAG, "I2C address changed successfully");
    return ESP_OK;
}

esp_err_t vl53l3cx_start_ranging(vl53l3cx_dev_t *dev)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting continuous ranging...");

    // Clear interrupt
    esp_err_t ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SYSTEM_INTERRUPT_CLEAR, 0x01);
    if (ret != ESP_OK) return ret;

    // Start BACKTOBACK mode (continuous ranging)
    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SYSTEM_MODE_START, VL53L3CX_MODE_START_BACKTOBACK);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ranging");
        return ret;
    }

    dev->measurement_active = true;
    ESP_LOGI(TAG, "Ranging started");
    return ESP_OK;
}

esp_err_t vl53l3cx_stop_ranging(vl53l3cx_dev_t *dev)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Stopping ranging...");

    // Write stop command twice (manufacturer recommendation)
    esp_err_t ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SYSTEM_MODE_START, VL53L3CX_MODE_START_STOP);
    if (ret != ESP_OK) return ret;

    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SYSTEM_MODE_START, VL53L3CX_MODE_START_STOP);
    if (ret != ESP_OK) return ret;

    // Clear interrupt
    ret = vl53l3cx_write_byte(dev, VL53L3CX_REG_SYSTEM_INTERRUPT_CLEAR, 0x01);
    if (ret != ESP_OK) return ret;

    dev->measurement_active = false;
    ESP_LOGI(TAG, "Ranging stopped");
    return ESP_OK;
}

esp_err_t vl53l3cx_wait_data_ready(vl53l3cx_dev_t *dev, uint32_t timeout_ms)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t int_status;
    uint32_t start_time = MILLIS();

    do {
        esp_err_t ret = vl53l3cx_read_byte(dev, VL53L3CX_REG_RESULT_INTERRUPT_STATUS, &int_status);
        if (ret != ESP_OK) {
            return ret;
        }

        if ((MILLIS() - start_time) > timeout_ms) {
            ESP_LOGW(TAG, "Data ready timeout");
            return ESP_ERR_TIMEOUT;
        }

        DELAY_MS(VL53L3CX_POLL_INTERVAL_MS);

    } while ((int_status & 0x20) == 0);  // bit 5: NEW_DATA_READY

    return ESP_OK;
}

esp_err_t vl53l3cx_get_ranging_data(vl53l3cx_dev_t *dev, vl53l3cx_result_t *result)
{
    if (dev == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Read histogram data (77 bytes total)
    uint8_t histogram_buffer[VL53L3CX_HISTOGRAM_TOTAL_SIZE];
    esp_err_t ret = vl53l3cx_read_reg(dev, VL53L3CX_REG_RESULT_INTERRUPT_STATUS,
                                       histogram_buffer, VL53L3CX_HISTOGRAM_TOTAL_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read histogram data");
        return ret;
    }

    // Extract header information
    result->range_status = histogram_buffer[1] & 0x1F;
    result->stream_count = histogram_buffer[3];

    // Extract histogram bins (24 bins × 3 bytes each)
    for (int bin = 0; bin < VL53L3CX_HISTOGRAM_BINS; bin++) {
        int offset = VL53L3CX_HISTOGRAM_HEADER_SIZE + (bin * 3);
        result->bin_data[bin] = ((uint32_t)histogram_buffer[offset + 0] << 16) |
                                ((uint32_t)histogram_buffer[offset + 1] << 8) |
                                ((uint32_t)histogram_buffer[offset + 2]);
    }

    // Calculate ambient estimate (average of first 6 bins)
    uint32_t ambient_sum = 0;
    for (int i = 0; i < 6; i++) {
        ambient_sum += result->bin_data[i];
    }
    result->ambient_estimate = ambient_sum / 6;

    // Ambient removal
    uint32_t corrected_bins[VL53L3CX_HISTOGRAM_BINS];
    for (int i = 0; i < VL53L3CX_HISTOGRAM_BINS; i++) {
        if (result->bin_data[i] > result->ambient_estimate) {
            corrected_bins[i] = result->bin_data[i] - result->ambient_estimate;
        } else {
            corrected_bins[i] = 0;
        }
    }

    // Peak detection (search bins 6-17)
    uint32_t max_count = 0;
    uint8_t peak_bin = 0;
    for (int i = 6; i < 18; i++) {
        if (corrected_bins[i] > max_count) {
            max_count = corrected_bins[i];
            peak_bin = i;
        }
    }
    result->peak_bin = peak_bin;

    // Distance calculation with sub-bin interpolation
    float distance_mm = 0.0f;

    if (max_count > 0 && peak_bin > 0 && peak_bin < 23) {
        // Sub-bin interpolation using parabolic fit
        int32_t a = corrected_bins[peak_bin - 1];
        int32_t b = corrected_bins[peak_bin];
        int32_t c = corrected_bins[peak_bin + 1];

        int32_t denominator = a - 2*b + c;
        float sub_bin_offset = 0.0f;

        if (denominator != 0) {
            sub_bin_offset = 0.5f * (float)(a - c) / (float)denominator;
        }

        float accurate_bin = (float)peak_bin + sub_bin_offset;

        // Bin width depends on VCSEL period
        // Period A (bins 0-11): ~15.0 mm/bin
        // Period B (bins 12-23): ~12.5 mm/bin
        float bin_width_mm = (peak_bin < 12) ? 15.0f : 12.5f;
        distance_mm = accurate_bin * bin_width_mm;
    }

    result->distance_mm = (uint16_t)distance_mm;

    // Clear interrupt
    ret = vl53l3cx_clear_interrupt(dev);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to clear interrupt");
    }

    return ESP_OK;
}

esp_err_t vl53l3cx_clear_interrupt(vl53l3cx_dev_t *dev)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return vl53l3cx_write_byte(dev, VL53L3CX_REG_SYSTEM_INTERRUPT_CLEAR, 0x01);
}

const char* vl53l3cx_get_range_status_string(uint8_t status)
{
    switch (status) {
        case VL53L3CX_RANGE_STATUS_RANGE_VALID:
            return "Range Valid";
        case VL53L3CX_RANGE_STATUS_SIGMA_FAIL:
            return "Sigma Fail";
        case VL53L3CX_RANGE_STATUS_SIGNAL_FAIL:
            return "Signal Fail";
        case VL53L3CX_RANGE_STATUS_RANGE_VALID_MIN_RANGE_CLIPPED:
            return "Min Range Clipped";
        case VL53L3CX_RANGE_STATUS_OUTOFBOUNDS_FAIL:
            return "Out of Bounds";
        case VL53L3CX_RANGE_STATUS_HARDWARE_FAIL:
            return "Hardware Fail";
        case VL53L3CX_RANGE_STATUS_RANGE_VALID_NO_WRAP_CHECK_FAIL:
            return "No Wrap Check";
        case VL53L3CX_RANGE_STATUS_WRAP_TARGET_FAIL:
            return "Wrap Target Fail";
        case VL53L3CX_RANGE_STATUS_PROCESSING_FAIL:
            return "Processing Fail";
        case VL53L3CX_RANGE_STATUS_XTALK_SIGNAL_FAIL:
            return "Crosstalk Fail";
        case VL53L3CX_RANGE_STATUS_SYNCRONISATION_INT:
            return "Sync Interrupt";
        case VL53L3CX_RANGE_STATUS_RANGE_VALID_MERGED_PULSE:
            return "Merged Pulse";
        case VL53L3CX_RANGE_STATUS_TARGET_PRESENT_LACK_OF_SIGNAL:
            return "Lack of Signal";
        case VL53L3CX_RANGE_STATUS_MIN_RANGE_FAIL:
            return "Min Range Fail";
        case VL53L3CX_RANGE_STATUS_RANGE_INVALID:
            return "Range Invalid";
        default:
            return "Unknown Status";
    }
}
