/**
 * @file vl53lx_outlier_filter.h
 * @brief VL53LX Outlier Detection and Filtering
 *
 * Provides algorithms for removing outliers from ToF sensor measurements:
 * - Moving median filter
 * - Moving average filter
 * - Range status validation
 * - Rate-of-change limiter
 */

#ifndef VL53LX_OUTLIER_FILTER_H
#define VL53LX_OUTLIER_FILTER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Filter types
 */
typedef enum {
    VL53LX_FILTER_MEDIAN,      ///< Moving median filter (best for outliers)
    VL53LX_FILTER_AVERAGE,     ///< Moving average filter (smooth)
    VL53LX_FILTER_WEIGHTED_AVG,///< Weighted average filter
    VL53LX_FILTER_KALMAN       ///< 1D Kalman filter (optimal estimation)
} vl53lx_filter_type_t;

/**
 * @brief Filter configuration
 */
typedef struct {
    vl53lx_filter_type_t filter_type;   ///< Type of filter to apply
    uint8_t window_size;                 ///< Filter window size (3-15, not used for Kalman)
    bool enable_status_check;            ///< Enable range status validation
    bool enable_rate_limit;              ///< Enable rate-of-change limiter
    uint16_t max_change_rate_mm;         ///< Maximum change rate (mm) between samples
    uint8_t valid_status_mask;           ///< Bitmask of valid range statuses (default: 0x01 for status 0 only)

    // Kalman filter specific parameters
    float kalman_process_noise;          ///< Process noise covariance Q (default: 0.01)
    float kalman_measurement_noise;      ///< Measurement noise covariance R (default: 4.0)
} vl53lx_filter_config_t;

/**
 * @brief Filter state structure
 */
typedef struct {
    vl53lx_filter_config_t config;       ///< Filter configuration
    uint16_t *buffer;                    ///< Circular buffer for samples
    uint8_t *status_buffer;              ///< Buffer for range statuses
    uint8_t head;                        ///< Buffer head index
    uint8_t count;                       ///< Number of valid samples in buffer
    uint16_t last_output;                ///< Last filtered output value
    uint8_t rejected_count;              ///< Consecutive rejected samples count

    // Kalman filter state
    float kalman_x;                      ///< Estimated state (distance)
    float kalman_p;                      ///< Estimation error covariance
    bool kalman_initialized;             ///< Kalman filter initialized flag

    bool initialized;                    ///< Filter initialized flag
} vl53lx_filter_t;

/**
 * @brief Initialize outlier filter with default configuration
 *
 * @param filter Pointer to filter structure
 * @return true if successful, false otherwise
 */
bool VL53LX_FilterInit(vl53lx_filter_t *filter);

/**
 * @brief Initialize outlier filter with custom configuration
 *
 * @param filter Pointer to filter structure
 * @param config Pointer to configuration
 * @return true if successful, false otherwise
 */
bool VL53LX_FilterInitWithConfig(vl53lx_filter_t *filter, const vl53lx_filter_config_t *config);

/**
 * @brief Deinitialize filter and free resources
 *
 * @param filter Pointer to filter structure
 */
void VL53LX_FilterDeinit(vl53lx_filter_t *filter);

/**
 * @brief Reset filter state (clear buffer)
 *
 * @param filter Pointer to filter structure
 */
void VL53LX_FilterReset(vl53lx_filter_t *filter);

/**
 * @brief Process new measurement through filter
 *
 * @param filter Pointer to filter structure
 * @param distance_mm Raw distance measurement (mm)
 * @param range_status Range status from sensor
 * @param output_mm Pointer to store filtered output
 * @return true if output is valid, false if filtered out
 */
bool VL53LX_FilterUpdate(vl53lx_filter_t *filter, uint16_t distance_mm, uint8_t range_status, uint16_t *output_mm);

/**
 * @brief Get default filter configuration
 *
 * @return Default configuration structure
 */
vl53lx_filter_config_t VL53LX_FilterGetDefaultConfig(void);

/**
 * @brief Check if range status is valid
 *
 * @param range_status Range status from sensor
 * @return true if valid, false otherwise
 */
bool VL53LX_FilterIsValidRangeStatus(uint8_t range_status);

#ifdef __cplusplus
}
#endif

#endif // VL53LX_OUTLIER_FILTER_H
