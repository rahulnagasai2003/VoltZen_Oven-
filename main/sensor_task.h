#pragma once
/*
 * sensor_task.h — Oven Unit
 *
 * FreeRTOS task that:
 *  - Reads temperature every 1 second
 *  - Reads fan currents every 5 seconds
 *  - Publishes telemetry to MQTT every 5 seconds
 */

#include "esp_err.h"
#include "max31856.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the sensor task.
 *
 * @param dev    Pointer to initialized MAX31856 device handle.
 */
esp_err_t sensor_task_start(max31856_t *dev);

/**
 * @brief Get the latest temperature reading.
 */
float sensor_task_get_temperature(void);

/**
 * @brief Get the latest fan currents.
 */
void sensor_task_get_fans(float *fan1, float *fan2);

#ifdef __cplusplus
}
#endif
