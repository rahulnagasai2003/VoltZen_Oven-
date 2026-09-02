#include "wifi_prov_mgr.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "lwip/sockets.h"

static const char *TAG = "WIFI_PROV";

#define WIFI_CONNECT_TIMEOUT_MS 30000

static char saved_ssid[64] = {0};
static char saved_password[128] = {0};
static char saved_device_id[64] = {0};
static char saved_mqtt_host[128] = {0};
static int  saved_mqtt_port = 1883;
static char saved_mqtt_user[64] = {0};
static char saved_mqtt_pass[64] = {0};
static bool is_connected = false;

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static httpd_handle_t s_server = NULL;

static void save_credentials(const char *ssid, const char *pass, const char *dev_id, 
                             const char *m_host, int m_port, const char *m_user, const char *m_pass) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_str(my_handle, "ssid", ssid);
        nvs_set_str(my_handle, "password", pass);
        nvs_set_str(my_handle, "device_id", dev_id);
        nvs_set_str(my_handle, "mqttHost", m_host);
        nvs_set_i32(my_handle, "mqttPort", m_port);
        nvs_set_str(my_handle, "mqttUsername", m_user);
        nvs_set_str(my_handle, "mqttPassword", m_pass);
        nvs_set_u8(my_handle, "valid", 0xAB);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Credentials saved to NVS.");
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for saving credentials");
    }
}

static bool load_credentials(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err != ESP_OK) return false;

    uint8_t valid = 0;
    err = nvs_get_u8(my_handle, "valid", &valid);
    if (err != ESP_OK || valid != 0xAB) {
        nvs_close(my_handle);
        return false;
    }

    size_t size;
    
    size = sizeof(saved_ssid);
    nvs_get_str(my_handle, "ssid", saved_ssid, &size);
    
    size = sizeof(saved_password);
    nvs_get_str(my_handle, "password", saved_password, &size);
    
    size = sizeof(saved_device_id);
    nvs_get_str(my_handle, "device_id", saved_device_id, &size);

    size = sizeof(saved_mqtt_host);
    nvs_get_str(my_handle, "mqttHost", saved_mqtt_host, &size);

    int32_t port = 1883;
    nvs_get_i32(my_handle, "mqttPort", &port);
    saved_mqtt_port = port;

    size = sizeof(saved_mqtt_user);
    nvs_get_str(my_handle, "mqttUsername", saved_mqtt_user, &size);

    size = sizeof(saved_mqtt_pass);
    nvs_get_str(my_handle, "mqttPassword", saved_mqtt_pass, &size);
    
    nvs_close(my_handle);
    ESP_LOGI(TAG, "Loaded SSID: %s, DeviceID: %s", saved_ssid, saved_device_id);
    return true;
}

// Asynchronous reboot task so the HTTP server can finish sending the response
static void delayed_reboot_task(void *arg) {
    ESP_LOGI(TAG, "Configuration received. Rebooting in 2s...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

static esp_err_t config_post_handler(httpd_req_t *req) {
    int total_len = req->content_len;
    if (total_len >= 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body too large");
        return ESP_FAIL;
    }
    
    char *buf = malloc(total_len + 1);
    if(!buf) return ESP_FAIL;
    
    int ret = httpd_req_recv(req, buf, total_len);
    if (ret <= 0) {
        free(buf);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[total_len] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    
    if (root == NULL) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\":\"Invalid JSON\"}");
        return ESP_FAIL;
    }
    
    cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass_item = cJSON_GetObjectItem(root, "password");
    cJSON *dev_item  = cJSON_GetObjectItem(root, "deviceId");
    cJSON *host_item = cJSON_GetObjectItem(root, "mqttHost");
    cJSON *port_item = cJSON_GetObjectItem(root, "mqttPort");
    cJSON *user_item = cJSON_GetObjectItem(root, "mqttUsername");
    cJSON *mpass_item = cJSON_GetObjectItem(root, "mqttPassword");
    
    if (!ssid_item || !cJSON_IsString(ssid_item) || strlen(ssid_item->valuestring) == 0) {
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\":\"ssid is required\"}");
        return ESP_FAIL;
    }
    
    const char *ssid = ssid_item->valuestring;
    const char *pass = (pass_item && cJSON_IsString(pass_item)) ? pass_item->valuestring : "";
    const char *dev_id = (dev_item && cJSON_IsString(dev_item)) ? dev_item->valuestring : "";
    const char *m_host = (host_item && cJSON_IsString(host_item)) ? host_item->valuestring : "";
    int m_port = (port_item && cJSON_IsNumber(port_item)) ? port_item->valueint : 1883;
    const char *m_user = (user_item && cJSON_IsString(user_item)) ? user_item->valuestring : "";
    const char *m_pass = (mpass_item && cJSON_IsString(mpass_item)) ? mpass_item->valuestring : "";
    
    // Print all received details for debugging
    ESP_LOGI(TAG, "==== Received Provisioning Data ====");
    ESP_LOGI(TAG, "SSID: %s", ssid);
    ESP_LOGI(TAG, "WiFi Password: %s", pass);
    ESP_LOGI(TAG, "Device ID: %s", dev_id);
    ESP_LOGI(TAG, "MQTT Host: %s", m_host);
    ESP_LOGI(TAG, "MQTT Port: %d", m_port);
    ESP_LOGI(TAG, "MQTT User: %s", m_user);
    ESP_LOGI(TAG, "MQTT Pass: %s", m_pass);
    ESP_LOGI(TAG, "====================================");
    
    // Extract the client's IP address (the Flutter App's IP) to show in the monitor
    int sockfd = httpd_req_to_sockfd(req);
    char client_ip[32] = "unknown";
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    if (sockfd >= 0 && getpeername(sockfd, (struct sockaddr *)&client_addr, &addr_len) == 0) {
        inet_ntoa_r(client_addr.sin_addr, client_ip, sizeof(client_ip));
    }

    // Send success response
    const char *resp = "{\"status\":\"received\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr(req, resp);
    
    // Explicitly print to the monitor that we sent it to the specific IP!
    ESP_LOGI(TAG, "Successfully sent HTTP ACK to App at IP %s: %s", client_ip, resp);
    
    save_credentials(ssid, pass, dev_id, m_host, m_port, m_user, m_pass);
    cJSON_Delete(root);
    
    // Create a task to reboot the device asynchronously so the HTTP request finishes cleanly
    xTaskCreate(delayed_reboot_task, "delayed_reboot_task", 2048, NULL, 5, NULL);
    
    return ESP_OK;
}

static const httpd_uri_t config_uri = {
    .uri       = "/api/wifi/configure",
    .method    = HTTP_POST,
    .handler   = config_post_handler,
    .user_ctx  = NULL
};

static void start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    
    if (httpd_start(&s_server, &config) == ESP_OK) {
        httpd_register_uri_handler(s_server, &config_uri);
        ESP_LOGI(TAG, "HTTP server started for provisioning on /api/wifi/configure");
    } else {
        ESP_LOGE(TAG, "Error starting HTTP server!");
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "Retry connecting to AP");
        esp_wifi_connect();
        is_connected = false;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        is_connected = true;
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

static void start_ap_mode(void) {
    ESP_LOGI(TAG, "Starting SoftAP mode...");
    
    s_ap_netif = esp_netif_create_default_wifi_ap();

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    char ap_ssid[32];
    snprintf(ap_ssid, sizeof(ap_ssid), "NLX-APTEMP-%02X%02X", mac[4], mac[5]);

    wifi_config_t wifi_config = {
        .ap = {
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };
    strncpy((char *)wifi_config.ap.ssid, ap_ssid, sizeof(wifi_config.ap.ssid) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "SoftAP started. SSID: %s", ap_ssid);
    
    start_webserver();
}

static void connect_sta_mode(void) {
    ESP_LOGI(TAG, "Starting STA mode to connect to %s", saved_ssid);
    s_sta_netif = esp_netif_create_default_wifi_sta();

    s_wifi_event_group = xEventGroupCreate();

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, saved_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, saved_password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_prov_mgr_init(void) {
    if (load_credentials() && strlen(saved_ssid) > 0) {
        connect_sta_mode();
    } else {
        start_ap_mode();
    }
}

bool wifi_prov_mgr_is_connected(void) {
    return is_connected;
}

bool wifi_prov_mgr_get_mqtt_details(char *host, int max_host_len,
                                    int *port,
                                    char *user, int max_user_len,
                                    char *pass, int max_pass_len,
                                    char *device_id, int max_dev_id_len)
{
    if (strlen(saved_mqtt_host) == 0) return false;
    
    if (host) strncpy(host, saved_mqtt_host, max_host_len);
    if (port) *port = saved_mqtt_port;
    if (user) strncpy(user, saved_mqtt_user, max_user_len);
    if (pass) strncpy(pass, saved_mqtt_pass, max_pass_len);
    if (device_id) strncpy(device_id, saved_device_id, max_dev_id_len);
    return true;
}
