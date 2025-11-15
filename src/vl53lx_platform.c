/**
 * @file vl53lx_platform.c
 * @brief VL53LX Platform Layer Implementation for ESP-IDF
 *
 * Implements I2C communication functions using ESP-IDF's new I2C master API
 */

#include "vl53lx_platform.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "VL53LX_PLATFORM";

// I2C transaction timeout in milliseconds
#define VL53LX_I2C_TIMEOUT_MS  100

/**
 * @brief Initialize VL53LX platform
 */
int8_t VL53LX_PlatformInit(VL53LX_DEV pdev, i2c_master_bus_handle_t bus_handle, uint16_t device_address)
{
    if (pdev == NULL || bus_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return VL53LX_ERROR_INVALID_PARAMS;
    }

    // Configure I2C device
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_address,
        .scl_speed_hz = 400000,  // 400 kHz (Fast mode)
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &pdev->i2c_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        return VL53LX_ERROR_CONTROL_INTERFACE;
    }

    pdev->i2c_slave_address = device_address;
    ESP_LOGI(TAG, "VL53LX platform initialized at address 0x%02X", device_address);

    return VL53LX_ERROR_NONE;
}

/**
 * @brief Deinitialize VL53LX platform
 */
int8_t VL53LX_PlatformDeinit(VL53LX_DEV pdev)
{
    if (pdev == NULL || pdev->i2c_dev_handle == NULL) {
        return VL53LX_ERROR_INVALID_PARAMS;
    }

    esp_err_t ret = i2c_master_bus_rm_device(pdev->i2c_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove I2C device: %s", esp_err_to_name(ret));
        return VL53LX_ERROR_CONTROL_INTERFACE;
    }

    pdev->i2c_dev_handle = NULL;
    ESP_LOGI(TAG, "VL53LX platform deinitialized");

    return VL53LX_ERROR_NONE;
}

/**
 * @brief Write multiple bytes to VL53LX device
 *
 * VL53LX uses 16-bit register addresses in big-endian format
 */
int8_t VL53LX_WriteMulti(VL53LX_DEV pdev, uint16_t index, uint8_t *pdata, uint32_t count)
{
    if (pdev == NULL || pdev->i2c_dev_handle == NULL || pdata == NULL) {
        return VL53LX_ERROR_INVALID_PARAMS;
    }

    // Allocate buffer: 2 bytes for register address + data
    uint8_t *buffer = (uint8_t *)malloc(count + 2);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return VL53LX_ERROR_COMMS_BUFFER_TOO_SMALL;
    }

    // Register address in big-endian (MSB first)
    buffer[0] = (index >> 8) & 0xFF;
    buffer[1] = index & 0xFF;
    memcpy(&buffer[2], pdata, count);

    // Write data using new I2C master API
    esp_err_t ret = i2c_master_transmit(pdev->i2c_dev_handle, buffer, count + 2, VL53LX_I2C_TIMEOUT_MS);

    free(buffer);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed at 0x%04X: %s", index, esp_err_to_name(ret));
        return (ret == ESP_ERR_TIMEOUT) ? VL53LX_ERROR_TIME_OUT : VL53LX_ERROR_CONTROL_INTERFACE;
    }

    return VL53LX_ERROR_NONE;
}

/**
 * @brief Read multiple bytes from VL53LX device
 */
int8_t VL53LX_ReadMulti(VL53LX_DEV pdev, uint16_t index, uint8_t *pdata, uint32_t count)
{
    if (pdev == NULL || pdev->i2c_dev_handle == NULL || pdata == NULL) {
        return VL53LX_ERROR_INVALID_PARAMS;
    }

    // Register address in big-endian (MSB first)
    uint8_t reg_addr[2];
    reg_addr[0] = (index >> 8) & 0xFF;
    reg_addr[1] = index & 0xFF;

    // Write register address, then read data
    esp_err_t ret = i2c_master_transmit_receive(pdev->i2c_dev_handle,
                                                reg_addr, 2,
                                                pdata, count,
                                                VL53LX_I2C_TIMEOUT_MS);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed at 0x%04X: %s", index, esp_err_to_name(ret));
        return (ret == ESP_ERR_TIMEOUT) ? VL53LX_ERROR_TIME_OUT : VL53LX_ERROR_CONTROL_INTERFACE;
    }

    return VL53LX_ERROR_NONE;
}

/**
 * @brief Write single byte
 */
int8_t VL53LX_WriteByte(VL53LX_DEV pdev, uint16_t index, uint8_t data)
{
    return VL53LX_WriteMulti(pdev, index, &data, 1);
}

/**
 * @brief Write 16-bit word (big-endian)
 */
int8_t VL53LX_WriteWord(VL53LX_DEV pdev, uint16_t index, uint16_t data)
{
    uint8_t buffer[2];
    buffer[0] = (data >> 8) & 0xFF;  // MSB
    buffer[1] = data & 0xFF;         // LSB
    return VL53LX_WriteMulti(pdev, index, buffer, 2);
}

/**
 * @brief Write 32-bit double word (big-endian)
 */
int8_t VL53LX_WriteDWord(VL53LX_DEV pdev, uint16_t index, uint32_t data)
{
    uint8_t buffer[4];
    buffer[0] = (data >> 24) & 0xFF;  // MSB
    buffer[1] = (data >> 16) & 0xFF;
    buffer[2] = (data >> 8) & 0xFF;
    buffer[3] = data & 0xFF;          // LSB
    return VL53LX_WriteMulti(pdev, index, buffer, 4);
}

/**
 * @brief Read single byte
 */
int8_t VL53LX_ReadByte(VL53LX_DEV pdev, uint16_t index, uint8_t *pdata)
{
    return VL53LX_ReadMulti(pdev, index, pdata, 1);
}

/**
 * @brief Read 16-bit word (big-endian)
 */
int8_t VL53LX_ReadWord(VL53LX_DEV pdev, uint16_t index, uint16_t *pdata)
{
    uint8_t buffer[2];
    int8_t status = VL53LX_ReadMulti(pdev, index, buffer, 2);
    if (status == VL53LX_ERROR_NONE) {
        *pdata = ((uint16_t)buffer[0] << 8) | buffer[1];
    }
    return status;
}

/**
 * @brief Read 32-bit double word (big-endian)
 */
int8_t VL53LX_ReadDWord(VL53LX_DEV pdev, uint16_t index, uint32_t *pdata)
{
    uint8_t buffer[4];
    int8_t status = VL53LX_ReadMulti(pdev, index, buffer, 4);
    if (status == VL53LX_ERROR_NONE) {
        *pdata = ((uint32_t)buffer[0] << 24) |
                 ((uint32_t)buffer[1] << 16) |
                 ((uint32_t)buffer[2] << 8) |
                 buffer[3];
    }
    return status;
}

/**
 * @brief Wait for specified time
 */
int8_t VL53LX_WaitMs(VL53LX_DEV pdev, int32_t wait_ms)
{
    (void)pdev;  // Unused parameter

    if (wait_ms < 0) {
        return VL53LX_ERROR_INVALID_PARAMS;
    }

    vTaskDelay(pdMS_TO_TICKS(wait_ms));
    return VL53LX_ERROR_NONE;
}
