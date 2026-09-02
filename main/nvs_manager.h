#pragma once
/*
 * nvs_manager.h — Oven Unit
 *
 * All NVS read/write for the oven.
 * Namespace: "oven_cfg"
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// WiFi credentials (emergency use only — do NOT auto-connect on save)
esp_err_t nvs_save_wifi(const char *ssid, const char *password);
bool      nvs_load_wifi(char *ssid, size_t ssid_len, char *password, size_t pass_len);

// Thermocouple type ("J" or "K")
esp_err_t nvs_save_tc_type(const char *tc_type);
bool      nvs_load_tc_type(char *tc_type, size_t len);

// Temperature threshold
esp_err_t nvs_save_temp_thresholds(float temp_min, float temp_max);
bool      nvs_load_temp_thresholds(float *temp_min, float *temp_max);

// Last known sensor readings (persist across reboots for data continuity)
esp_err_t nvs_save_last_readings(float temperature, float fan1_curr, float fan2_curr);
bool      nvs_load_last_readings(float *temperature, float *fan1_curr, float *fan2_curr);

#ifdef __cplusplus
}
#endif
