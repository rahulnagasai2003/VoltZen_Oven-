#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Initializes WiFi. 
 * Checks NVS for credentials.
 * If found, starts STA mode and connects.
 * If not found, starts SoftAP and HTTP server for provisioning.
 */
void wifi_prov_mgr_init(void);

/**
 * Returns true if the device has successfully connected to STA WiFi.
 */
bool wifi_prov_mgr_is_connected(void);

/**
 * Get provisioned MQTT details.
 */
bool wifi_prov_mgr_get_mqtt_details(char *host, int max_host_len,
                                    int *port,
                                    char *user, int max_user_len,
                                    char *pass, int max_pass_len,
                                    char *device_id, int max_dev_id_len);
