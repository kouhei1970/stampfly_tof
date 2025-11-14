# StampFly ToF Driver for ESP32-S3

VL53L3CX Time-of-Flight distance sensor driver for M5StampFly, built on ESP-IDF.

## Features

- ✅ **Complete register-level implementation** based on VL53L3CX datasheet
- ✅ **Dual sensor support** (front and bottom sensors)
- ✅ **MEDIUM_RANGE preset mode** (0-3000mm range)
- ✅ **Histogram-based distance calculation** with sub-bin interpolation
- ✅ **Multi-sensor I2C address management** via XSHUT pin control
- ✅ **ESP-IDF native** - optimized for ESP32-S3
- ✅ **Continuous ranging mode** with 100ms measurement interval

## Hardware Configuration

### M5StampFly Pin Mapping

| Function | GPIO | Description |
|----------|------|-------------|
| I2C SDA | GPIO3 | Shared I2C bus |
| I2C SCL | GPIO4 | Shared I2C bus |
| Front XSHUT | GPIO9 | Front sensor shutdown control |
| Front INT | GPIO8 | Front sensor interrupt (optional) |
| Bottom XSHUT | GPIO7 | Bottom sensor shutdown control |
| Bottom INT | GPIO6 | Bottom sensor interrupt (optional) |

### Default I2C Addresses

- **Front sensor**: `0x30` (changed from default `0x29` during init)
- **Bottom sensor**: `0x31` (changed from default `0x29` during init)

## Quick Start

### Prerequisites

- ESP-IDF v5.0 or later
- M5StampFly hardware

### Installation

1. Clone this repository into your ESP-IDF components directory:

```bash
cd ~/esp
git clone <repository-url> stampfly_tof
```

2. Or add as a component to your existing project:

```bash
cd your_project/components
git clone <repository-url> stampfly_tof
```

### Building the Example

```bash
cd stampfly_tof/exsampls/basic

# Configure ESP-IDF environment
. $IDF_PATH/export.sh

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

## Usage

### Basic Example

```c
#include "stampfly_tof.h"

void app_main(void)
{
    // Initialize ToF system
    stampfly_tof_handle_t tof;
    ESP_ERROR_CHECK(stampfly_tof_init(&tof, I2C_NUM_0));

    // Start continuous ranging
    ESP_ERROR_CHECK(stampfly_tof_start_ranging(&tof, STAMPFLY_TOF_SENSOR_BOTH));

    // Main loop
    while (1) {
        stampfly_tof_dual_result_t result;

        // Get distance from both sensors
        if (stampfly_tof_get_dual_distance(&tof, &result) == ESP_OK) {
            if (result.front_status == VL53L3CX_RANGE_STATUS_RANGE_VALID) {
                printf("Front: %d mm\n", result.front_distance_mm);
            }
            if (result.bottom_status == VL53L3CX_RANGE_STATUS_RANGE_VALID) {
                printf("Bottom: %d mm\n", result.bottom_distance_mm);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

### Single Sensor Example

```c
#include "vl53l3cx.h"

void app_main(void)
{
    // Initialize I2C
    vl53l3cx_i2c_master_init(I2C_NUM_0, GPIO_NUM_3, GPIO_NUM_4, 400000);

    // Initialize single sensor
    vl53l3cx_dev_t sensor;
    vl53l3cx_init(&sensor, I2C_NUM_0, VL53L3CX_DEFAULT_I2C_ADDR);

    // Start ranging
    vl53l3cx_start_ranging(&sensor);

    // Read data
    vl53l3cx_result_t result;
    while (1) {
        if (vl53l3cx_wait_data_ready(&sensor, 2000) == ESP_OK) {
            vl53l3cx_get_ranging_data(&sensor, &result);
            printf("Distance: %d mm (status: %s)\n",
                   result.distance_mm,
                   vl53l3cx_get_range_status_string(result.range_status));
        }
    }
}
```

## API Reference

### StampFly High-Level API

#### Initialization

```c
esp_err_t stampfly_tof_init(stampfly_tof_handle_t *handle, i2c_port_t i2c_port);
```

Initializes the dual ToF sensor system. Performs:
1. I2C master initialization
2. GPIO configuration
3. Sequential sensor initialization with address change

#### Ranging Control

```c
esp_err_t stampfly_tof_start_ranging(stampfly_tof_handle_t *handle,
                                      stampfly_tof_sensor_t sensor);
```

Starts continuous ranging on selected sensor(s).

- `sensor`: `STAMPFLY_TOF_SENSOR_FRONT`, `STAMPFLY_TOF_SENSOR_BOTTOM`, or `STAMPFLY_TOF_SENSOR_BOTH`

```c
esp_err_t stampfly_tof_stop_ranging(stampfly_tof_handle_t *handle,
                                     stampfly_tof_sensor_t sensor);
```

Stops ranging on selected sensor(s).

#### Data Acquisition

```c
esp_err_t stampfly_tof_get_dual_distance(stampfly_tof_handle_t *handle,
                                          stampfly_tof_dual_result_t *result);
```

Retrieves distance from both sensors simultaneously.

```c
esp_err_t stampfly_tof_get_front_distance(stampfly_tof_handle_t *handle,
                                           vl53l3cx_result_t *result);
```

Retrieves distance from front sensor only.

```c
esp_err_t stampfly_tof_get_bottom_distance(stampfly_tof_handle_t *handle,
                                            vl53l3cx_result_t *result);
```

Retrieves distance from bottom sensor only.

### VL53L3CX Core API

#### Device Initialization

```c
esp_err_t vl53l3cx_init(vl53l3cx_dev_t *dev, i2c_port_t i2c_port, uint8_t i2c_addr);
```

Complete initialization sequence:
- Waits for firmware boot
- Reads NVM calibration data
- Configures MEDIUM_RANGE preset mode

#### I2C Address Management

```c
esp_err_t vl53l3cx_set_device_address(vl53l3cx_dev_t *dev, uint8_t new_addr);
```

Changes the I2C address (volatile, resets on power cycle).

#### Measurement Control

```c
esp_err_t vl53l3cx_start_ranging(vl53l3cx_dev_t *dev);
esp_err_t vl53l3cx_stop_ranging(vl53l3cx_dev_t *dev);
esp_err_t vl53l3cx_wait_data_ready(vl53l3cx_dev_t *dev, uint32_t timeout_ms);
esp_err_t vl53l3cx_get_ranging_data(vl53l3cx_dev_t *dev, vl53l3cx_result_t *result);
```

#### Status Utilities

```c
const char* vl53l3cx_get_range_status_string(uint8_t status);
```

Returns human-readable status string.

## Data Structures

### `vl53l3cx_result_t`

```c
typedef struct {
    uint16_t distance_mm;                // Measured distance (mm)
    uint8_t range_status;                // Range status code
    uint8_t stream_count;                // Measurement counter
    uint32_t bin_data[24];               // Raw histogram bins
    uint32_t ambient_estimate;           // Ambient light level
    uint8_t peak_bin;                    // Peak bin index
} vl53l3cx_result_t;
```

### `stampfly_tof_dual_result_t`

```c
typedef struct {
    uint16_t front_distance_mm;          // Front sensor distance
    uint8_t front_status;                // Front sensor status
    uint16_t bottom_distance_mm;         // Bottom sensor distance
    uint8_t bottom_status;               // Bottom sensor status
} stampfly_tof_dual_result_t;
```

## Range Status Codes

| Status Code | Description |
|-------------|-------------|
| `0x09` | **Range Valid** - Measurement successful |
| `0x01` | Sigma Fail - Low signal quality |
| `0x02` | Signal Fail - No target detected |
| `0x04` | Out of Bounds - Target too far/close |
| `0x05` | Hardware Fail - Sensor malfunction |

See `vl53l3cx.h` for complete status code list.

## Technical Details

### Initialization Sequence

1. **Firmware Boot** (~100-200ms)
   - Poll `FIRMWARE_SYSTEM_STATUS` register
   - Wait for bit 0 = 1

2. **NVM Calibration Data** (~5ms)
   - Disable firmware
   - Enable power force
   - Read oscillator frequency from NVM
   - Restore firmware

3. **MEDIUM_RANGE Preset** (~50 register writes)
   - Configure GPIO, SPAD, VCSEL parameters
   - Set timing (VCSEL periods, timeouts)
   - Configure ROI, thresholds, algorithms

4. **Multi-Sensor Setup**
   - Shutdown all sensors via XSHUT pins
   - Wake up sensor 1, initialize, change address to 0x30
   - Wake up sensor 2, initialize, change address to 0x31

### Distance Calculation

The driver implements histogram-based ranging with:

1. **Ambient Removal**: Averages first 6 bins as ambient estimate
2. **Peak Detection**: Finds maximum signal in bins 6-17
3. **Sub-bin Interpolation**: Parabolic fit for sub-bin accuracy
4. **Bin Width Calculation**:
   - Period A (bins 0-11): ~15.0 mm/bin
   - Period B (bins 12-23): ~12.5 mm/bin

Distance formula: `distance = (peak_bin + sub_bin_offset) × bin_width`

### Performance

- **Measurement time**: ~33ms (MEDIUM_RANGE mode)
- **Measurement interval**: 100ms (configurable)
- **Range**: 0-3000mm
- **Accuracy**: ±5% typical

## Troubleshooting

### Sensor Not Detected

- Check I2C wiring (SDA=GPIO3, SCL=GPIO4)
- Verify power supply (sensors require 2.8V I/O)
- Check XSHUT pin connections

### Incorrect Distance Readings

- Verify target is within 0-3000mm range
- Check for ambient light interference
- Ensure target has sufficient reflectivity

### I2C Communication Errors

- Reduce I2C clock speed (change `400000` to `100000` in init)
- Add external pull-up resistors (4.7kΩ recommended)
- Check for I2C bus conflicts

## References

- [VL53L3CX Datasheet](docs/VL53L3CX_doc_ja.md)
- [M5StampFly Specifications](docs/M5StamFly_spec_ja.md)
- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/)

## License

This project is provided as-is for use with M5StampFly hardware.

## Contributing

Contributions are welcome! Please submit pull requests or open issues for bugs and feature requests.
