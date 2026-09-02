#pragma once
/*
 * max31856.h — Oven Unit
 *
 * Custom SPI driver for the MAX31856 thermocouple-to-digital converter.
 *
 * Features:
 *  - Supports J and K thermocouple types (runtime-configurable)
 *  - Auto-convert mode (continuous conversion)
 *  - 19-bit linearized temperature from the chip's internal lookup table
 *  - Fault detection (open circuit, overvoltage, cold-junction range)
 *
 * Hardware wiring:
 *   MAX31856 SDI  (MOSI) ── ESP32 GPIO 23
 *   MAX31856 SDO  (MISO) ── ESP32 GPIO 19
 *   MAX31856 SCLK (SCK)  ── ESP32 GPIO 18
 *   MAX31856 CS          ── ESP32 GPIO  5
 *   MAX31856 VCC         ── 3.3V
 *   MAX31856 GND         ── GND
 */

#include "esp_err.h"
#include "driver/spi_master.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─── SPI Pin Configuration ────────────────────────────────────────────────────
#define MAX31856_MOSI_PIN   23
#define MAX31856_MISO_PIN   19
#define MAX31856_SCK_PIN    18
#define MAX31856_CS_PIN      5

// ─── Thermocouple Type Codes (CR1 register bits [3:0]) ───────────────────────
typedef enum {
    TC_TYPE_B = 0x00,
    TC_TYPE_E = 0x01,
    TC_TYPE_J = 0x02,   // J-type thermocouple
    TC_TYPE_K = 0x03,   // K-type thermocouple  (default)
    TC_TYPE_N = 0x04,
    TC_TYPE_R = 0x05,
    TC_TYPE_S = 0x06,
    TC_TYPE_T = 0x07,
} max31856_tc_type_t;

// ─── Device Handle ────────────────────────────────────────────────────────────
typedef struct {
    spi_device_handle_t spi;
    max31856_tc_type_t  tc_type;
} max31856_t;

/**
 * @brief Initialize SPI bus and MAX31856 device.
 *
 * @param dev      Pointer to max31856_t handle to populate.
 * @param tc_type  Thermocouple type to configure (TC_TYPE_J or TC_TYPE_K).
 * @return ESP_OK on success.
 */
esp_err_t max31856_init(max31856_t *dev, max31856_tc_type_t tc_type);

/**
 * @brief Change the thermocouple type at runtime.
 *        Writes directly to the CR1 register — no reboot needed.
 *
 * @param dev      Pointer to initialized device handle.
 * @param tc_type  New thermocouple type.
 */
esp_err_t max31856_set_type(max31856_t *dev, max31856_tc_type_t tc_type);

/**
 * @brief Resolve thermocouple type string ("J" or "K") to enum.
 *        Defaults to TC_TYPE_K for unknown strings.
 */
max31856_tc_type_t max31856_type_from_string(const char *type_str);

/**
 * @brief Read the linearized thermocouple temperature.
 *
 * @param dev   Pointer to initialized device handle.
 * @param temp  Output: temperature in degrees Celsius.
 * @return ESP_OK on success, ESP_FAIL on fault or SPI error.
 */
esp_err_t max31856_read_temp(max31856_t *dev, float *temp);

#ifdef __cplusplus
}
#endif
