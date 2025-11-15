/**
 * @file vl53lx_outlier_filter.c
 * @brief VL53LX Outlier Detection and Filtering Implementation
 */

#include "vl53lx_outlier_filter.h"
#include <stdlib.h>
#include <string.h>

// Default configuration values
#define DEFAULT_WINDOW_SIZE         5       // 5 samples
#define DEFAULT_MAX_CHANGE_RATE_MM  500     // 500mm max change between samples
#define DEFAULT_VALID_STATUS_MASK   0x01    // Only status 0 (valid) by default

/**
 * @brief Compare function for qsort (ascending order)
 */
static int compare_uint16(const void *a, const void *b)
{
    return (*(uint16_t*)a - *(uint16_t*)b);
}

/**
 * @brief Calculate median of buffer
 */
static uint16_t calculate_median(uint16_t *buffer, uint8_t count)
{
    if (count == 0) {
        return 0;
    }

    // Create temporary buffer for sorting
    uint16_t temp[15];  // Max window size
    memcpy(temp, buffer, count * sizeof(uint16_t));

    // Sort
    qsort(temp, count, sizeof(uint16_t), compare_uint16);

    // Return median
    if (count % 2 == 0) {
        return (temp[count/2 - 1] + temp[count/2]) / 2;
    } else {
        return temp[count/2];
    }
}

/**
 * @brief Calculate average of buffer
 */
static uint16_t calculate_average(uint16_t *buffer, uint8_t count)
{
    if (count == 0) {
        return 0;
    }

    uint32_t sum = 0;
    for (uint8_t i = 0; i < count; i++) {
        sum += buffer[i];
    }

    return sum / count;
}

/**
 * @brief Calculate weighted average of buffer (recent samples weighted more)
 */
static uint16_t calculate_weighted_average(uint16_t *buffer, uint8_t count, uint8_t head)
{
    if (count == 0) {
        return 0;
    }

    uint32_t weighted_sum = 0;
    uint32_t weight_sum = 0;

    for (uint8_t i = 0; i < count; i++) {
        // Most recent sample has highest weight
        uint8_t idx = (head + count - 1 - i) % count;
        uint8_t weight = count - i;  // Recent: count, oldest: 1

        weighted_sum += buffer[idx] * weight;
        weight_sum += weight;
    }

    return weighted_sum / weight_sum;
}

vl53lx_filter_config_t VL53LX_FilterGetDefaultConfig(void)
{
    vl53lx_filter_config_t config = {
        .filter_type = VL53LX_FILTER_MEDIAN,
        .window_size = DEFAULT_WINDOW_SIZE,
        .enable_status_check = true,
        .enable_rate_limit = true,
        .max_change_rate_mm = DEFAULT_MAX_CHANGE_RATE_MM,
        .valid_status_mask = DEFAULT_VALID_STATUS_MASK,
    };
    return config;
}

bool VL53LX_FilterInit(vl53lx_filter_t *filter)
{
    vl53lx_filter_config_t config = VL53LX_FilterGetDefaultConfig();
    return VL53LX_FilterInitWithConfig(filter, &config);
}

bool VL53LX_FilterInitWithConfig(vl53lx_filter_t *filter, const vl53lx_filter_config_t *config)
{
    if (filter == NULL || config == NULL) {
        return false;
    }

    // Validate window size
    if (config->window_size < 3 || config->window_size > 15) {
        return false;
    }

    // Copy configuration
    memcpy(&filter->config, config, sizeof(vl53lx_filter_config_t));

    // Allocate buffers
    filter->buffer = (uint16_t*)malloc(config->window_size * sizeof(uint16_t));
    filter->status_buffer = (uint8_t*)malloc(config->window_size * sizeof(uint8_t));

    if (filter->buffer == NULL || filter->status_buffer == NULL) {
        // Cleanup on failure
        if (filter->buffer) free(filter->buffer);
        if (filter->status_buffer) free(filter->status_buffer);
        return false;
    }

    // Initialize state
    filter->head = 0;
    filter->count = 0;
    filter->last_output = 0;
    filter->initialized = true;

    return true;
}

void VL53LX_FilterDeinit(vl53lx_filter_t *filter)
{
    if (filter == NULL || !filter->initialized) {
        return;
    }

    if (filter->buffer) {
        free(filter->buffer);
        filter->buffer = NULL;
    }

    if (filter->status_buffer) {
        free(filter->status_buffer);
        filter->status_buffer = NULL;
    }

    filter->initialized = false;
}

void VL53LX_FilterReset(vl53lx_filter_t *filter)
{
    if (filter == NULL || !filter->initialized) {
        return;
    }

    filter->head = 0;
    filter->count = 0;
    filter->last_output = 0;
}

bool VL53LX_FilterIsValidRangeStatus(uint8_t range_status)
{
    // Status 0 = Valid measurement
    // Status 1,2 = Signal/Sigma failures (might be usable)
    // Status 4+ = Invalid (out of range, timeout, etc.)
    return (range_status == 0);
}

bool VL53LX_FilterUpdate(vl53lx_filter_t *filter, uint16_t distance_mm, uint8_t range_status, uint16_t *output_mm)
{
    if (filter == NULL || !filter->initialized || output_mm == NULL) {
        return false;
    }

    // Check range status if enabled
    if (filter->config.enable_status_check) {
        if ((1 << range_status) & filter->config.valid_status_mask) {
            // Status is invalid, reject sample
            return false;
        }
    }

    // Check rate of change if enabled and filter has previous output
    if (filter->config.enable_rate_limit && filter->count > 0) {
        int32_t change = (int32_t)distance_mm - (int32_t)filter->last_output;
        if (abs(change) > filter->config.max_change_rate_mm) {
            // Change too large, reject sample
            return false;
        }
    }

    // Add sample to circular buffer
    filter->buffer[filter->head] = distance_mm;
    filter->status_buffer[filter->head] = range_status;
    filter->head = (filter->head + 1) % filter->config.window_size;

    if (filter->count < filter->config.window_size) {
        filter->count++;
    }

    // Need at least 3 samples for meaningful filtering
    if (filter->count < 3) {
        *output_mm = distance_mm;
        filter->last_output = distance_mm;
        return true;
    }

    // Apply selected filter
    uint16_t filtered_value;
    switch (filter->config.filter_type) {
        case VL53LX_FILTER_MEDIAN:
            filtered_value = calculate_median(filter->buffer, filter->count);
            break;

        case VL53LX_FILTER_AVERAGE:
            filtered_value = calculate_average(filter->buffer, filter->count);
            break;

        case VL53LX_FILTER_WEIGHTED_AVG:
            filtered_value = calculate_weighted_average(filter->buffer, filter->count, filter->head);
            break;

        default:
            filtered_value = distance_mm;
            break;
    }

    *output_mm = filtered_value;
    filter->last_output = filtered_value;

    return true;
}
