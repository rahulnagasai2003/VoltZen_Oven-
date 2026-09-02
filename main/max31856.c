#include "max31856.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "MAX31856";

// ─── MAX31856 Register Map ────────────────────────────────────────────────────
#define REG_CR0         0x00    // Configuration Register 0
#define REG_CR1         0x01    // Configuration Register 1 (TC type)
#define REG_MASK        0x02    // Fault Mask Register
#define REG_CJHF        0x03    // Cold-Junction High Fault Threshold
#define REG_CJLF        0x04    // Cold-Junction Low Fault Threshold
#define REG_LTHFTH      0x05    // Linearized Temp High Fault Thresh (MSB)
#define REG_LTHFTL      0x06    // Linearized Temp High Fault Thresh (LSB)
#define REG_LTLFTH      0x07    // Linearized Temp Low Fault Thresh (MSB)
#define REG_LTLFTL      0x08    // Linearized Temp Low Fault Thresh (LSB)
#define REG_CJTO        0x09    // Cold-Junction Temperature Offset
#define REG_CJTH        0x0A    // Cold-Junction Temperature (MSB)
#define REG_CJTL        0x0B    // Cold-Junction Temperature (LSB)
#define REG_LTCBH       0x0C    // Linearized TC Temp Byte 2 (MSB)
#define REG_LTCBM       0x0D    // Linearized TC Temp Byte 1
#define REG_LTCBL       0x0E    // Linearized TC Temp Byte 0 (LSB)
#define REG_SR          0x0F    // Status Register (faults)

// Write address = register | 0x80
#define WRITE_ADDR(r)   ((r) | 0x80)

// CR0 flags
#define CR0_AUTOCONVERT 0x80    // Enable continuous auto-convert mode
#define CR0_ONESHOT     0x40

// Fault status bits (SR register)
#define SR_FAULT_CJ_RANGE   0x80
#define SR_FAULT_TC_RANGE   0x40
#define SR_FAULT_CJ_HIGH    0x20
#define SR_FAULT_CJ_LOW     0x10
#define SR_FAULT_TC_HIGH    0x08
#define SR_FAULT_TC_LOW     0x04
#define SR_FAULT_OV_UV      0x02
#define SR_FAULT_OPEN       0x01

#include "driver/gpio.h"
#include "esp_rom_sys.h"

// ─── SPI Helpers ──────────────────────────────────────────────────────────────
static esp_err_t spi_write_reg(max31856_t *dev, uint8_t reg, uint8_t value)
{
    uint8_t tx_buf[2] = { WRITE_ADDR(reg), value };
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = tx_buf,
    };
    return spi_device_polling_transmit(dev->spi, &t);
}

static esp_err_t spi_read_regs(max31856_t *dev, uint8_t start_reg,
                                uint8_t *buf, size_t len)
{
    // max31856 reads up to 3 bytes + 1 address byte = 4 bytes max.
    // This fits perfectly in the 4-byte tx_data / rx_data arrays.
    if (len > 3) return ESP_ERR_INVALID_ARG; 

    uint8_t tx_buf[4] = { start_reg, 0, 0, 0 };
    uint8_t rx_buf[4] = { 0, 0, 0, 0 };
    
    spi_transaction_t t = {
        .length    = (len + 1) * 8,
        .rxlength  = (len + 1) * 8,
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };
    
    esp_err_t ret = spi_device_polling_transmit(dev->spi, &t);
    
    if (ret == ESP_OK) {
        memcpy(buf, &rx_buf[1], len);
    }
    return ret;
}

// ─── Init ─────────────────────────────────────────────────────────────────────
esp_err_t max31856_init(max31856_t *dev, max31856_tc_type_t tc_type)
{
    spi_bus_config_t bus_cfg = {
        .miso_io_num      = MAX31856_MISO_PIN,
        .mosi_io_num      = MAX31856_MOSI_PIN,
        .sclk_io_num      = MAX31856_SCK_PIN,
        .quadwp_io_num    = -1,
        .quadhd_io_num    = -1,
        .max_transfer_sz  = 32,
    };
    // Disable DMA! The MAX31856 only transfers 4 bytes max. 
    // Using DMA with unaligned stack buffers causes random data corruption.
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_DISABLED));

    spi_device_interface_config_t dev_cfg = {
        .mode           = 1,            // MAX31856 uses SPI Mode 1 (CPOL=0, CPHA=1)
        .clock_speed_hz = 500 * 1000,   // 500 kHz (matches Arduino Adafruit perfectly)
        .spics_io_num   = MAX31856_CS_PIN, // Use hardware CS
        .queue_size     = 1,
        .cs_ena_pretrans = 1,           // 1 SPI clock setup time before transmission
    };
    esp_err_t ret = spi_bus_add_device(SPI3_HOST, &dev_cfg, &dev->spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    dev->tc_type = tc_type;

    // --- STRICT WRITE TEST (Write to CJ offset register and read back) ---
    uint8_t test_write_val = 0xAA;
    spi_write_reg(dev, REG_CJTO, test_write_val);
    uint8_t read_back = 0;
    spi_read_regs(dev, REG_CJTO, &read_back, 1);
    if (read_back == test_write_val) {
        ESP_LOGI(TAG, "STRICT WRITE TEST PASSED! SPI TX is 100%% working.");
    } else {
        ESP_LOGE(TAG, "STRICT WRITE TEST FAILED! Wrote 0x%02X, Read 0x%02X. SPI TX is broken!", test_write_val, read_back);
    }
    // Clear the offset register back to 0
    spi_write_reg(dev, REG_CJTO, 0x00);
    // ----------------------------------------------------------------------

    // Configure CR1: set thermocouple type in bits [3:0]
    ret = spi_write_reg(dev, REG_CR1, (uint8_t)tc_type & 0x0F);
    if (ret != ESP_OK) return ret;

    // Mask all faults (like Adafruit does) to prevent conversion locks
    spi_write_reg(dev, REG_MASK, 0x00);

    // Read-modify-write CR0 (like Adafruit does) to start conversions safely
    uint8_t cr0 = 0;
    spi_read_regs(dev, REG_CR0, &cr0, 1);
    cr0 |= CR0_AUTOCONVERT;
    ret = spi_write_reg(dev, REG_CR0, cr0);
    if (ret != ESP_OK) return ret;

    // Allow first conversion to complete (~200 ms for auto-convert mode)
    vTaskDelay(pdMS_TO_TICKS(250));

    // --- SPI HARDWARE TEST ---
    uint8_t test_cr1 = 0;
    spi_read_regs(dev, REG_CR1, &test_cr1, 1);
    ESP_LOGI(TAG, "SPI TEST: Wrote 0x%02X to CR1, Read back 0x%02X", (uint8_t)tc_type & 0x0F, test_cr1);
    if (test_cr1 != ((uint8_t)tc_type & 0x0F)) {
        ESP_LOGE(TAG, "SPI TEST FAILED! MISO wire is disconnected or chip is frozen.");
    } else {
        ESP_LOGI(TAG, "SPI TEST PASSED! MISO wire is working.");
    }

    ESP_LOGI(TAG, "MAX31856 initialized. TC type: %s (code 0x%02X)",
             (tc_type == TC_TYPE_J) ? "J" : (tc_type == TC_TYPE_K) ? "K" : "other",
             (uint8_t)tc_type);
    return ESP_OK;
}

// ─── Runtime Type Change ──────────────────────────────────────────────────────
esp_err_t max31856_set_type(max31856_t *dev, max31856_tc_type_t tc_type)
{
    if (dev->tc_type == tc_type) return ESP_OK;   // No change needed

    // Disable auto-convert before changing type (recommended by datasheet)
    spi_write_reg(dev, REG_CR0, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Write new thermocouple type to CR1 bits [3:0]
    esp_err_t ret = spi_write_reg(dev, REG_CR1, (uint8_t)tc_type & 0x0F);
    if (ret != ESP_OK) return ret;

    // Re-enable auto-convert ONLY
    spi_write_reg(dev, REG_CR0, CR0_AUTOCONVERT);
    vTaskDelay(pdMS_TO_TICKS(250));

    dev->tc_type = tc_type;
    ESP_LOGI(TAG, "TC type changed to: %s",
             (tc_type == TC_TYPE_J) ? "J" : (tc_type == TC_TYPE_K) ? "K" : "other");
    return ESP_OK;
}

max31856_tc_type_t max31856_type_from_string(const char *type_str)
{
    if (type_str && type_str[0] == 'J') return TC_TYPE_J;
    if (type_str && type_str[0] == 'K') return TC_TYPE_K;
    ESP_LOGW(TAG, "Unknown TC type '%s' — defaulting to K", type_str ? type_str : "NULL");
    return TC_TYPE_K;
}

// ─── Read Temperature ─────────────────────────────────────────────────────────
esp_err_t max31856_read_temp(max31856_t *dev, float *temp)
{
    if (!dev || !temp) return ESP_ERR_INVALID_ARG;

    // Read fault register first
    uint8_t sr = 0;
    spi_read_regs(dev, REG_SR, &sr, 1);
    if (sr & SR_FAULT_OPEN) {
        ESP_LOGE(TAG, "Fault: Open thermocouple (SR=0x%02X)", sr);
        return ESP_FAIL;
    }
    if (sr & SR_FAULT_OV_UV) {
        ESP_LOGE(TAG, "Fault: Overvoltage/Undervoltage (SR=0x%02X)", sr);
        return ESP_FAIL;
    }
    if (sr & (SR_FAULT_TC_RANGE | SR_FAULT_CJ_RANGE)) {
        ESP_LOGE(TAG, "Fault: Temperature out of range (SR=0x%02X)", sr);
        return ESP_FAIL;
    }

    // Read 3 bytes of linearized TC temperature: LTCBH, LTCBM, LTCBL
    uint8_t raw[3];
    esp_err_t ret = spi_read_regs(dev, REG_LTCBH, raw, 3);
    if (ret != ESP_OK) return ret;

    // --- DEBUG: Read Cold Junction and CR0 ---
    uint8_t cr0 = 0;
    spi_read_regs(dev, REG_CR0, &cr0, 1);
    uint8_t cj[2];
    spi_read_regs(dev, REG_CJTH, cj, 2);
    // -----------------------------------------

    // Combine 3 bytes into 24-bit signed value
    // Bits [23:5] are the temperature (19-bit, MSB first)
    // Resolution: 0.0078125 °C / LSB  (1/128 of a degree)
    int32_t raw_val = ((int32_t)raw[0] << 16) | ((int32_t)raw[1] << 8) | raw[2];
    
    // Sign-extend if negative (bit 23 is the sign bit)
    if (raw_val & 0x800000) {
        raw_val |= 0xFF000000;
    }
    
    // Shift out the 5 bottom dead bits
    raw_val >>= 5;
    
    *temp = (float)raw_val * 0.0078125f;

    ESP_LOGI(TAG, "Temperature: %.2f °C (raw bytes=0x%02X 0x%02X 0x%02X -> val=0x%06X, SR=0x%02X, CR0=0x%02X, CJ=0x%02X%02X)",
             *temp, raw[0], raw[1], raw[2], (unsigned)(raw_val & 0xFFFFFF), sr, cr0, cj[0], cj[1]);
    return ESP_OK;
}
