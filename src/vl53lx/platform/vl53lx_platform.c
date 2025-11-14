/**
 * @file vl53lx_platform.c
 * @brief ESP-IDF Platform Layer for VL53LX Driver
 *
 * This file implements the platform-specific functions required by the
 * VL53LX driver core using ESP-IDF APIs.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "vl53lx_platform.h"
#include "vl53lx_platform_user_data.h"
#include "vl53lx_error_codes.h"

static const char *TAG = "VL53LX_PLATFORM";

/*
 * I2C Communication Functions
 */

VL53LX_Error VL53LX_WriteMulti(VL53LX_Dev_t *pdev, uint16_t index, uint8_t *pdata, uint32_t count)
{
    if (pdev == NULL || pdata == NULL) {
        return VL53LX_ERROR_INVALID_PARAMS;
    }

    // Prepare write buffer: [index_msb, index_lsb, data...]
    uint8_t *write_buf = malloc(count + 2);
    if (write_buf == NULL) {
        return VL53LX_ERROR_CONTROL_INTERFACE;
    }

    write_buf[0] = (index >> 8) & 0xFF;  // MSB
    write_buf[1] = index & 0xFF;          // LSB
    memcpy(&write_buf[2], pdata, count);

    esp_err_t ret = i2c_master_transmit(pdev->i2c_dev, write_buf, count + 2, 100);

    free(write_buf);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed at 0x%04X: %s", index, esp_err_to_name(ret));
        return VL53LX_ERROR_CONTROL_INTERFACE;
    }

    return VL53LX_ERROR_NONE;
}

VL53LX_Error VL53LX_ReadMulti(VL53LX_Dev_t *pdev, uint16_t index, uint8_t *pdata, uint32_t count)
{
    if (pdev == NULL || pdata == NULL) {
        return VL53LX_ERROR_INVALID_PARAMS;
    }

    // Prepare register address buffer
    uint8_t reg_addr[2] = {
        (index >> 8) & 0xFF,  // MSB
        index & 0xFF           // LSB
    };

    esp_err_t ret = i2c_master_transmit_receive(pdev->i2c_dev, reg_addr, 2, pdata, count, 100);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed at 0x%04X: %s", index, esp_err_to_name(ret));
        return VL53LX_ERROR_CONTROL_INTERFACE;
    }

    return VL53LX_ERROR_NONE;
}

VL53LX_Error VL53LX_WrByte(VL53LX_Dev_t *pdev, uint16_t index, uint8_t data)
{
    return VL53LX_WriteMulti(pdev, index, &data, 1);
}

VL53LX_Error VL53LX_WrWord(VL53LX_Dev_t *pdev, uint16_t index, uint16_t data)
{
    uint8_t buf[2];
    buf[0] = (data >> 8) & 0xFF;  // MSB first (big-endian)
    buf[1] = data & 0xFF;          // LSB
    return VL53LX_WriteMulti(pdev, index, buf, 2);
}

VL53LX_Error VL53LX_WrDWord(VL53LX_Dev_t *pdev, uint16_t index, uint32_t data)
{
    uint8_t buf[4];
    buf[0] = (data >> 24) & 0xFF;  // MSB first (big-endian)
    buf[1] = (data >> 16) & 0xFF;
    buf[2] = (data >> 8) & 0xFF;
    buf[3] = data & 0xFF;           // LSB
    return VL53LX_WriteMulti(pdev, index, buf, 4);
}

VL53LX_Error VL53LX_RdByte(VL53LX_Dev_t *pdev, uint16_t index, uint8_t *pdata)
{
    return VL53LX_ReadMulti(pdev, index, pdata, 1);
}

VL53LX_Error VL53LX_RdWord(VL53LX_Dev_t *pdev, uint16_t index, uint16_t *pdata)
{
    uint8_t buf[2];
    VL53LX_Error status = VL53LX_ReadMulti(pdev, index, buf, 2);
    if (status == VL53LX_ERROR_NONE) {
        *pdata = ((uint16_t)buf[0] << 8) | buf[1];  // Big-endian
    }
    return status;
}

VL53LX_Error VL53LX_RdDWord(VL53LX_Dev_t *pdev, uint16_t index, uint32_t *pdata)
{
    uint8_t buf[4];
    VL53LX_Error status = VL53LX_ReadMulti(pdev, index, buf, 4);
    if (status == VL53LX_ERROR_NONE) {
        *pdata = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                 ((uint32_t)buf[2] << 8) | buf[3];  // Big-endian
    }
    return status;
}

/*
 * Communication Management Functions
 */

VL53LX_Error VL53LX_CommsInitialise(VL53LX_Dev_t *pdev, uint8_t comms_type, uint16_t comms_speed_khz)
{
    if (pdev == NULL) {
        return VL53LX_ERROR_INVALID_PARAMS;
    }

    // Store communication parameters
    pdev->comms_type = comms_type;
    pdev->comms_speed_khz = comms_speed_khz;

    // I2C initialization is handled at higher level (stampfly_tof.c)
    // This function just stores the parameters

    return VL53LX_ERROR_NONE;
}

VL53LX_Error VL53LX_CommsClose(VL53LX_Dev_t *pdev)
{
    // I2C cleanup is handled at higher level
    return VL53LX_ERROR_NONE;
}

/*
 * Timing Functions
 */

VL53LX_Error VL53LX_WaitUs(VL53LX_Dev_t *pdev, int32_t wait_us)
{
    (void)pdev;  // Unused parameter

    if (wait_us > 0) {
        esp_rom_delay_us((uint32_t)wait_us);
    }

    return VL53LX_ERROR_NONE;
}

VL53LX_Error VL53LX_WaitMs(VL53LX_Dev_t *pdev, int32_t wait_ms)
{
    (void)pdev;  // Unused parameter

    if (wait_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }

    return VL53LX_ERROR_NONE;
}

VL53LX_Error VL53LX_GetTimerFrequency(int32_t *ptimer_freq_hz)
{
    if (ptimer_freq_hz == NULL) {
        return VL53LX_ERROR_INVALID_PARAMS;
    }

    // ESP32 uses microsecond timer
    *ptimer_freq_hz = 1000000;  // 1 MHz

    return VL53LX_ERROR_NONE;
}

VL53LX_Error VL53LX_GetTimerValue(int32_t *ptimer_count)
{
    if (ptimer_count == NULL) {
        return VL53LX_ERROR_INVALID_PARAMS;
    }

    // Return current microsecond count
    *ptimer_count = (int32_t)(esp_timer_get_time() & 0x7FFFFFFF);

    return VL53LX_ERROR_NONE;
}

VL53LX_Error VL53LX_GetTickCount(VL53LX_Dev_t *pdev, uint32_t *ptime_ms)
{
    (void)pdev;  // Unused parameter

    if (ptime_ms == NULL) {
        return VL53LX_ERROR_INVALID_PARAMS;
    }

    // Return current millisecond count
    *ptime_ms = (uint32_t)(esp_timer_get_time() / 1000);

    return VL53LX_ERROR_NONE;
}

/*
 * GPIO Functions (Stubs - GPIO is handled at higher level in stampfly_tof.c)
 */

VL53LX_Error VL53LX_GpioSetMode(uint8_t pin, uint8_t mode)
{
    // GPIO configuration is handled at higher level
    (void)pin;
    (void)mode;
    return VL53LX_ERROR_NONE;
}

VL53LX_Error VL53LX_GpioSetValue(uint8_t pin, uint8_t value)
{
    // GPIO control is handled at higher level
    (void)pin;
    (void)value;
    return VL53LX_ERROR_NONE;
}

VL53LX_Error VL53LX_GpioGetValue(uint8_t pin, uint8_t *pvalue)
{
    // GPIO reading is handled at higher level
    (void)pin;
    if (pvalue != NULL) {
        *pvalue = 0;
    }
    return VL53LX_ERROR_NONE;
}

VL53LX_Error VL53LX_GpioXshutdown(uint8_t value)
{
    // XSHUT control is handled at higher level (stampfly_tof.c)
    (void)value;
    return VL53LX_ERROR_NONE;
}

VL53LX_Error VL53LX_GpioCommsSelect(uint8_t value)
{
    // Not used (I2C only)
    (void)value;
    return VL53LX_ERROR_NONE;
}

VL53LX_Error VL53LX_GpioPowerEnable(uint8_t value)
{
    // Power control is handled at hardware level
    (void)value;
    return VL53LX_ERROR_NONE;
}

VL53LX_Error VL53LX_GpioInterruptEnable(void (*function)(void), uint8_t edge_type)
{
    // Interrupt handling is done at higher level (stampfly_tof.c)
    (void)function;
    (void)edge_type;
    return VL53LX_ERROR_NONE;
}

VL53LX_Error VL53LX_GpioInterruptDisable(void)
{
    // Interrupt handling is done at higher level
    return VL53LX_ERROR_NONE;
}

/*
 * Utility Functions
 */

VL53LX_Error VL53LX_WaitValueMaskEx(
    VL53LX_Dev_t *pdev,
    uint32_t      timeout_ms,
    uint16_t      index,
    uint8_t       value,
    uint8_t       mask,
    uint32_t      poll_delay_ms)
{
    uint32_t start_time_ms = 0;
    uint32_t current_time_ms = 0;
    uint8_t byte_value = 0;
    VL53LX_Error status = VL53LX_ERROR_NONE;

    // Get start time
    status = VL53LX_GetTickCount(pdev, &start_time_ms);
    if (status != VL53LX_ERROR_NONE) {
        return status;
    }

    // Poll until value matches or timeout
    do {
        status = VL53LX_RdByte(pdev, index, &byte_value);
        if (status != VL53LX_ERROR_NONE) {
            return status;
        }

        // Check if masked value matches
        if ((byte_value & mask) == value) {
            return VL53LX_ERROR_NONE;
        }

        // Wait before next poll
        if (poll_delay_ms > 0) {
            status = VL53LX_WaitMs(pdev, poll_delay_ms);
            if (status != VL53LX_ERROR_NONE) {
                return status;
            }
        }

        // Get current time
        status = VL53LX_GetTickCount(pdev, &current_time_ms);
        if (status != VL53LX_ERROR_NONE) {
            return status;
        }

    } while ((current_time_ms - start_time_ms) < timeout_ms);

    // Timeout occurred
    ESP_LOGW(TAG, "WaitValueMaskEx timeout at 0x%04X (expected: 0x%02X, mask: 0x%02X, got: 0x%02X)",
             index, value, mask, byte_value);

    return VL53LX_ERROR_TIME_OUT;
}
