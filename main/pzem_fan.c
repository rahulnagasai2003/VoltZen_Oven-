#include "pzem_fan.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "PZEM_FAN";

static const struct {
    int     tx_gpio;
    int     rx_gpio;
    uint8_t addr;
    const char *name;
} fan_cfg[FAN_COUNT] = {
    [FAN_1] = { FAN_TX_FAN1_PIN, FAN_RX_FAN1_PIN, FAN_ADDR_FAN1, "Fan-1" },
    [FAN_2] = { FAN_TX_FAN2_PIN, FAN_RX_FAN2_PIN, FAN_ADDR_FAN2, "Fan-2" },
};

// ─── CRC-16 Modbus ────────────────────────────────────────────────────────────
static uint16_t crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc & 0x0001) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
        }
    }
    return crc;
}

// ─── Init ─────────────────────────────────────────────────────────────────────
esp_err_t pzem_fan_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = FAN_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };

    esp_err_t ret = uart_driver_install(FAN_UART_PORT, 512, 0, 0, NULL, 0);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "UART install failed"); return ret; }

    ret = uart_param_config(FAN_UART_PORT, &cfg);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "UART param config failed"); return ret; }

    ret = uart_set_pin(FAN_UART_PORT, FAN_TX_FAN1_PIN, FAN_RX_FAN1_PIN,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "UART set pin failed"); return ret; }

    ESP_LOGI(TAG, "Fan PZEM UART initialized. F1(TX=%d,RX=%d) F2(TX=%d,RX=%d)",
             FAN_TX_FAN1_PIN, FAN_RX_FAN1_PIN, FAN_TX_FAN2_PIN, FAN_RX_FAN2_PIN);
    return ESP_OK;
}

// ─── Read Current ─────────────────────────────────────────────────────────────
esp_err_t pzem_fan_read(fan_id_t fan, float *current)
{
    if (fan >= FAN_COUNT || !current) return ESP_ERR_INVALID_ARG;

    // Switch TX/RX pin to this fan's PZEM
    uart_set_pin(FAN_UART_PORT, fan_cfg[fan].tx_gpio, fan_cfg[fan].rx_gpio,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Modbus RTU: Read Input Registers, 10 registers from 0x0000
    // We only need register 0x0001 (current low word) and 0x0002 (current high word)
    // but we read all 10 to be compatible with standard PZEM-004T response
    uint8_t request[8];
    request[0] = fan_cfg[fan].addr;
    request[1] = 0x04;
    request[2] = 0x00; request[3] = 0x00;   // Start register
    request[4] = 0x00; request[5] = 0x0A;   // Count: 10 registers
    uint16_t crc = crc16(request, 6);
    request[6] = (uint8_t)(crc & 0xFF);
    request[7] = (uint8_t)((crc >> 8) & 0xFF);

    uart_flush_input(FAN_UART_PORT);

    int written = uart_write_bytes(FAN_UART_PORT, (const char *)request, 8);
    if (written != 8) {
        ESP_LOGE(TAG, "[%s] UART write failed", fan_cfg[fan].name);
        return ESP_FAIL;
    }

    uint8_t response[25];
    int received = uart_read_bytes(FAN_UART_PORT, response, 25, pdMS_TO_TICKS(1000));
    if (received < 25) {
        ESP_LOGE(TAG, "[%s] Timeout (%d/25 bytes)", fan_cfg[fan].name, received);
        return ESP_FAIL;
    }

    uint16_t rx_crc   = (uint16_t)response[24] << 8 | response[23];
    uint16_t calc_crc = crc16(response, 23);
    if (rx_crc != calc_crc) {
        ESP_LOGE(TAG, "[%s] CRC error", fan_cfg[fan].name);
        return ESP_FAIL;
    }

    // Parse registers: current = reg[1] (low) | reg[2] (high), resolution 0.001 A
    uint16_t reg[10];
    for (int i = 0; i < 10; i++) {
        reg[i] = (uint16_t)response[3 + i * 2] << 8 | response[3 + i * 2 + 1];
    }
    *current = ((uint32_t)reg[2] << 16 | reg[1]) / 1000.0f;

    ESP_LOGI(TAG, "[%s] Current: %.3f A", fan_cfg[fan].name, *current);
    return ESP_OK;
}
