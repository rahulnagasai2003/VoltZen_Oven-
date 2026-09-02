#pragma once
/*
 * pzem_fan.h — Oven Unit
 *
 * Reads current from two PZEM-004T V3 modules connected to fan circuits.
 * Uses the same shared-TX / switched-RX approach as the supply unit.
 *
 * Hardware wiring:
 *   ESP GPIO 17 (TX) ──► RX pin of BOTH fan PZEMs (shared)
 *   Fan-1 PZEM TX    ──► ESP GPIO 16
 *   Fan-2 PZEM TX    ──► ESP GPIO 21
 *
 * One-time Modbus address programming:
 *   Fan-1 address = 0x01
 *   Fan-2 address = 0x02
 */

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─── Pin / UART Configuration
// ─────────────────────────────────────────────────
#define FAN_UART_PORT UART_NUM_2
#define FAN_TX_FAN1_PIN 17
#define FAN_RX_FAN1_PIN 16
#define FAN_TX_FAN2_PIN 22
#define FAN_RX_FAN2_PIN 21
#define FAN_BAUD_RATE 9600

#define FAN_ADDR_FAN1 0x01
#define FAN_ADDR_FAN2 0x02

typedef enum { FAN_1 = 0, FAN_2, FAN_COUNT } fan_id_t;

/**
 * @brief Initialize UART2 for fan PZEM communication.
 */
esp_err_t pzem_fan_init(void);

/**
 * @brief Read current (in Amperes) from one fan PZEM.
 *
 * @param fan     Which fan (FAN_1 or FAN_2).
 * @param current Output: current in Amperes.
 * @return ESP_OK on success, ESP_FAIL on timeout / CRC error.
 */
esp_err_t pzem_fan_read(fan_id_t fan, float *current);

#ifdef __cplusplus
}
#endif
