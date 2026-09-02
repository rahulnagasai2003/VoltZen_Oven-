/*
 * main.c — Oven Unit ESP32
 *
 * Startup sequence:
 *   1. NVS init + load saved TC type, last data
 *   2. MAX31856 SPI init
 *   3. PZEM fan init
 *   4. WiFi Provisioning Init (Checks NVS for creds, connects STA or starts AP)
 *   5. Wait for WiFi connection
 *   6. Start MQTT Client
 *   7. Start Sensor task
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "max31856.h"
#include "pzem_fan.h"
#include "nvs_manager.h"
#include "sensor_task.h"
#include "wifi_prov_mgr.h"
#include "mqtt_mgr.h"

static const char *TAG = "MAIN_OVEN";

// ─── Global device handle
max31856_t g_max31856 = {0};

static void main_startup_task(void *arg)
{
    // Wait until WiFi is connected (either immediately from NVS or after provisioning reboot)
    while (!wifi_prov_mgr_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "WiFi Connected! Starting MQTT and Sensors...");

    mqtt_mgr_init();

    // Give MQTT a moment to connect
    vTaskDelay(pdMS_TO_TICKS(2000));

    sensor_task_start(&g_max31856);

    vTaskDelete(NULL);
}

// ─── Entry Point ──────────────────────────────────────────────────────────────
void app_main(void)
{
    ESP_LOGI(TAG, "=== Oven Unit Starting ===");

    // 1. NVS init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Load TC type from NVS
    char tc_type_str[4] = "K";
    nvs_load_tc_type(tc_type_str, sizeof(tc_type_str));
    max31856_tc_type_t tc_type = max31856_type_from_string(tc_type_str);
    ESP_LOGI(TAG, "TC type from NVS: %s", tc_type_str);

    // Load last known sensor readings
    float last_temp = 0.0f, last_f1 = 0.0f, last_f2 = 0.0f;
    if (nvs_load_last_readings(&last_temp, &last_f1, &last_f2)) {
        ESP_LOGI(TAG, "Last readings from NVS: temp=%.1f°C fan1=%.2fA fan2=%.2fA",
                 last_temp, last_f1, last_f2);
    }

    // 2. MAX31856 SPI init
    ret = max31856_init(&g_max31856, tc_type);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MAX31856 init failed! Check SPI wiring.");
    }

    // 3. Fan PZEM UART init
    ret = pzem_fan_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fan PZEM init failed! Check UART wiring.");
    }

    // Initialize Network interfaces and Event Loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    // 4. WiFi Provisioning
    wifi_prov_mgr_init();

    // 5. Start startup task to wait for WiFi and then launch MQTT/Sensors
    xTaskCreate(main_startup_task, "main_startup_task", 4096, NULL, 5, NULL);
}
