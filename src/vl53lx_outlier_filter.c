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
        .kalman_process_noise = 0.01f,      // Q: Low process noise (sensor is stationary)
        .kalman_measurement_noise = 4.0f,   // R: Measurement noise based on sensor spec
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
    filter->rejected_count = 0;
    filter->samples_since_reset = 0;

    // Initialize Kalman filter state
    filter->kalman_x = 0.0f;
    filter->kalman_p = 1000.0f;  // High initial uncertainty
    filter->kalman_initialized = false;

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
    filter->rejected_count = 0;
    filter->samples_since_reset = 0;

    // Reset Kalman filter
    filter->kalman_x = 0.0f;
    filter->kalman_p = 1000.0f;
    filter->kalman_initialized = false;
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

    bool status_valid = true;
    bool rate_valid = true;

    // Check range status if enabled
    if (filter->config.enable_status_check) {
        if (!((1 << range_status) & filter->config.valid_status_mask)) {
            // Status is invalid
            status_valid = false;
            filter->rejected_count++;

            // If too many consecutive rejections, reset filter
            if (filter->rejected_count >= 5) {
                VL53LX_FilterReset(filter);
            }
        }
    }

    // Check rate of change if enabled and filter has previous output
    bool has_previous_output = (filter->config.filter_type == VL53LX_FILTER_KALMAN)
                               ? filter->kalman_initialized
                               : (filter->count > 0);

    if (filter->config.enable_rate_limit && has_previous_output) {
        int32_t change = (int32_t)distance_mm - (int32_t)filter->last_output;

        // After reset, allow larger changes for first few samples
        uint16_t effective_rate_limit = filter->config.max_change_rate_mm;
        if (filter->samples_since_reset < 3) {
            effective_rate_limit = filter->config.max_change_rate_mm * 3;  // 3x more lenient
        }

        if (abs(change) > effective_rate_limit) {
            // Change too large
            rate_valid = false;
            filter->rejected_count++;

            // If too many consecutive rejections, reset filter to accept new baseline
            if (filter->rejected_count >= 5) {
                VL53LX_FilterReset(filter);
            }
        }
    }

    // Sample accepted (either valid or will do prediction-only), reset rejection counter if fully valid
    if (status_valid && rate_valid) {
        filter->rejected_count = 0;
    }

    // Apply selected filter
    uint16_t filtered_value;

    if (filter->config.filter_type == VL53LX_FILTER_KALMAN) {
        // Kalman filter doesn't use buffer, process directly
        if (!filter->kalman_initialized) {
            // Initialize with first measurement (only if valid)
            if (status_valid && rate_valid) {
                filter->kalman_x = (float)distance_mm;
                filter->kalman_p = filter->config.kalman_measurement_noise;
                filter->kalman_initialized = true;
                filtered_value = distance_mm;
            } else {
                // Cannot initialize with invalid sample
                return false;
            }
        } else {
            // Kalman filter update
            float Q = filter->config.kalman_process_noise;
            float R = filter->config.kalman_measurement_noise;

            // Prediction step (always execute)
            float x_pred = filter->kalman_x;  // No state transition for stationary model
            float p_pred = filter->kalman_p + Q;

            if (status_valid && rate_valid) {
                // Observation valid: perform full update (prediction + observation)
                float K = p_pred / (p_pred + R);  // Kalman gain
                float z = (float)distance_mm;      // Measurement
                filter->kalman_x = x_pred + K * (z - x_pred);
                filter->kalman_p = (1.0f - K) * p_pred;
            } else {
                // Observation invalid: prediction only (uncertainty increases)
                filter->kalman_x = x_pred;
                filter->kalman_p = p_pred;  // Uncertainty grows
            }

            filtered_value = (uint16_t)(filter->kalman_x + 0.5f);  // Round to nearest integer
        }
    } else {
        // Buffer-based filters (median, average, weighted average)
        // Reject invalid samples for buffer-based filters
        if (!status_valid || !rate_valid) {
            return false;
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
            filtered_value = distance_mm;
        } else {
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
        }
    }

    *output_mm = filtered_value;
    filter->last_output = filtered_value;

    // Increment samples since reset only for fully valid samples (cap at 255 to prevent overflow)
    if (status_valid && rate_valid && filter->samples_since_reset < 255) {
        filter->samples_since_reset++;
    }

    return true;
}
