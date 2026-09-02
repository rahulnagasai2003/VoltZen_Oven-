#pragma once

#include <stdbool.h>

/**
 * Initialize and start the MQTT client using the provisioned details.
 */
void mqtt_mgr_init(void);

/**
 * Publish telemetry data to the broker.
 * Topic: nt/v1/{deviceId}/stat/telemetry
 */
void mqtt_mgr_publish(float temperature, bool fan1_state, bool fan2_state);
