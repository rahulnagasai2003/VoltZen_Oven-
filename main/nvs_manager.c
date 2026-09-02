#include "nvs_manager.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG    = "NVS_OVEN";
static const char *NVS_NS = "oven_cfg";

// ─── WiFi ─────────────────────────────────────────────────────────────────────
esp_err_t nvs_save_wifi(const char *ssid, const char *password)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "password", password);
    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Emergency WiFi creds saved (not connected) SSID=%s", ssid);
    return err;
}

bool nvs_load_wifi(char *ssid, size_t ssid_len, char *password, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = (nvs_get_str(h, "ssid", ssid, &ssid_len) == ESP_OK) &&
              (nvs_get_str(h, "password", password, &pass_len) == ESP_OK);
    nvs_close(h);
    return ok;
}

// ─── TC Type ──────────────────────────────────────────────────────────────────
esp_err_t nvs_save_tc_type(const char *tc_type)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_str(h, "tc_type", tc_type);
    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "TC type saved: %s", tc_type);
    return err;
}

bool nvs_load_tc_type(char *tc_type, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = (nvs_get_str(h, "tc_type", tc_type, &len) == ESP_OK);
    nvs_close(h);
    return ok;
}

// ─── Temperature Thresholds ───────────────────────────────────────────────────
typedef struct { float temp_min, temp_max; } temp_thresholds_t;

esp_err_t nvs_save_temp_thresholds(float temp_min, float temp_max)
{
    temp_thresholds_t t = { temp_min, temp_max };
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_blob(h, "temp_thresh", &t, sizeof(t));
    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Temp thresholds saved: Min=%.1f°C Max=%.1f°C", temp_min, temp_max);
    return err;
}

bool nvs_load_temp_thresholds(float *temp_min, float *temp_max)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    temp_thresholds_t t = {0};
    size_t sz = sizeof(t);
    bool ok = (nvs_get_blob(h, "temp_thresh", &t, &sz) == ESP_OK);
    nvs_close(h);
    if (ok) {
        if (temp_min) *temp_min = t.temp_min;
        if (temp_max) *temp_max = t.temp_max;
    }
    return ok;
}

// ─── Last Readings ────────────────────────────────────────────────────────────
typedef struct { float temp, fan1, fan2; } last_readings_t;

esp_err_t nvs_save_last_readings(float temperature, float fan1_curr, float fan2_curr)
{
    last_readings_t lr = { temperature, fan1_curr, fan2_curr };
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_blob(h, "last_data", &lr, sizeof(lr));
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool nvs_load_last_readings(float *temperature, float *fan1_curr, float *fan2_curr)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    last_readings_t lr = {0};
    size_t sz = sizeof(lr);
    bool ok = (nvs_get_blob(h, "last_data", &lr, &sz) == ESP_OK);
    nvs_close(h);
    if (ok) {
        *temperature = lr.temp;
        *fan1_curr   = lr.fan1;
        *fan2_curr   = lr.fan2;
    }
    return ok;
}
