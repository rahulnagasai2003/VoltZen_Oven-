#include "mqtt_mgr.h"
#include "wifi_prov_mgr.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include <string.h>

static const char *TAG = "MQTT_MGR";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_mqtt_connected = false;
static char s_device_id[64] = {0};

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            s_mqtt_connected = true;
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            s_mqtt_connected = false;
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            break;
    }
}

void mqtt_mgr_init(void)
{
    char host[128];
    int port;
    char user[64];
    char pass[64];

    if (!wifi_prov_mgr_get_mqtt_details(host, sizeof(host), &port,
                                        user, sizeof(user),
                                        pass, sizeof(pass),
                                        s_device_id, sizeof(s_device_id))) {
        ESP_LOGE(TAG, "Failed to get MQTT details from provisioning");
        return;
    }

    char broker_uri[256];
    const char *protocol = (port == 8883) ? "mqtts" : "mqtt";
    snprintf(broker_uri, sizeof(broker_uri), "%s://%s:%d", protocol, host, port);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
        .broker.verification.certificate = NULL, // Must be NULL to trigger the insecure bypass
        .broker.verification.skip_cert_common_name_check = true, // We enabled INSECURE TLS in sdkconfig, so this works!
        .credentials.username = user[0] ? user : NULL,
        .credentials.authentication.password = pass[0] ? pass : NULL,
        .credentials.client_id = s_device_id
    };

    ESP_LOGI(TAG, "Initializing MQTT to %s", broker_uri);

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client) {
        esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
        esp_mqtt_client_start(s_client);
    }
}

void mqtt_mgr_publish(float temperature, bool fan1_state, bool fan2_state)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "temperature", temperature);

    cJSON *fans_array = cJSON_CreateArray();
    
    cJSON *fan1 = cJSON_CreateObject();
    cJSON_AddNumberToObject(fan1, "index", 1);
    cJSON_AddBoolToObject(fan1, "state", fan1_state);
    cJSON_AddItemToArray(fans_array, fan1);

    cJSON *fan2 = cJSON_CreateObject();
    cJSON_AddNumberToObject(fan2, "index", 2);
    cJSON_AddBoolToObject(fan2, "state", fan2_state);
    cJSON_AddItemToArray(fans_array, fan2);

    cJSON_AddItemToObject(root, "fans", fans_array);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (payload) {
        // Print the JSON payload to the Serial Monitor so you can see it
        ESP_LOGI(TAG, "Generated MQTT Payload: %s", payload);

        // Only attempt to send if we are fully connected
        if (s_client && s_mqtt_connected && strlen(s_device_id) > 0) {
            char topic[128];
            snprintf(topic, sizeof(topic), "nt/v1/%s/stat/telemetry", s_device_id);

            int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
            ESP_LOGD(TAG, "Published telemetry to %s, msg_id=%d", topic, msg_id);
        } else {
            ESP_LOGW(TAG, "MQTT not connected. Payload was NOT sent to broker.");
        }
        
        free(payload);
    }
}
