#include "sensor_task.h"
#include "pzem_fan.h"
#include "mqtt_mgr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "SENSOR";

#define TEMP_READ_INTERVAL_MS   1000               // 1 second
#define FAN_READ_INTERVAL_MS    5000               // 5 seconds
#define PERIODIC_SEND_MS        5000               // 5 seconds

// ─── Shared State ─────────────────────────────────────────────────────────────
static max31856_t   *s_dev              = NULL;
static float         s_temperature      = 0.0f;
static float         s_fan1_current     = 0.0f;
static float         s_fan2_current     = 0.0f;
static SemaphoreHandle_t s_data_mux     = NULL;

// ─── Internal: Send Oven Data ─────────────────────────────────────────────────
static void send_oven_data(void)
{
    float temp, f1, f2;
    xSemaphoreTake(s_data_mux, portMAX_DELAY);
    temp = s_temperature;
    f1   = s_fan1_current;
    f2   = s_fan2_current;
    xSemaphoreGive(s_data_mux);

    bool fan1_state = (f1 > 0.05f);
    bool fan2_state = (f2 > 0.05f);

    mqtt_mgr_publish(temp, fan1_state, fan2_state);
}

// ─── Main Sensor Task ─────────────────────────────────────────────────────────
static void sensor_task(void *arg)
{
    uint32_t fan_timer      = 0;
    uint32_t periodic_timer = 0;

    ESP_LOGI(TAG, "Sensor task started");

    while (1) {
        // ── 1-second temperature read ──────────────────────────────────────
        float temp = 0.0f;
        esp_err_t ret = max31856_read_temp(s_dev, &temp);

        if (ret == ESP_OK) {
            xSemaphoreTake(s_data_mux, portMAX_DELAY);
            s_temperature = temp;
            xSemaphoreGive(s_data_mux);

            ESP_LOGD(TAG, "Temp: %.2f°C", temp);
        } else {
            ESP_LOGE(TAG, "MAX31856 read error");
        }

        fan_timer      += TEMP_READ_INTERVAL_MS;
        periodic_timer += TEMP_READ_INTERVAL_MS;

        // ── 5-second fan current read ──────────────────────────────────────
        if (fan_timer >= FAN_READ_INTERVAL_MS) {
            fan_timer = 0;
            float f1 = 0.0f, f2 = 0.0f;

            esp_err_t r1 = pzem_fan_read(FAN_1, &f1);
            esp_err_t r2 = pzem_fan_read(FAN_2, &f2);

            xSemaphoreTake(s_data_mux, portMAX_DELAY);
            if (r1 == ESP_OK) s_fan1_current = f1;
            else s_fan1_current = 0.0f; // Optional: reset if disconnected
            
            if (r2 == ESP_OK) s_fan2_current = f2;
            else s_fan2_current = 0.0f; // Optional: reset if disconnected
            xSemaphoreGive(s_data_mux);
        }

        // ── 5-second periodic send ─────────────────────────────────────────
        if (periodic_timer >= PERIODIC_SEND_MS) {
            periodic_timer = 0;
            ESP_LOGD(TAG, "5-second periodic send");
            send_oven_data();
        }

        vTaskDelay(pdMS_TO_TICKS(TEMP_READ_INTERVAL_MS));
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────
esp_err_t sensor_task_start(max31856_t *dev)
{
    s_dev      = dev;
    s_data_mux = xSemaphoreCreateMutex();

    xTaskCreate(sensor_task, "sensor_task", 8192, NULL, 5, NULL);
    return ESP_OK;
}

float sensor_task_get_temperature(void)
{
    xSemaphoreTake(s_data_mux, portMAX_DELAY);
    float t = s_temperature;
    xSemaphoreGive(s_data_mux);
    return t;
}

void sensor_task_get_fans(float *fan1, float *fan2)
{
    xSemaphoreTake(s_data_mux, portMAX_DELAY);
    if (fan1) *fan1 = s_fan1_current;
    if (fan2) *fan2 = s_fan2_current;
    xSemaphoreGive(s_data_mux);
}
