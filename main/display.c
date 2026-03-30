/*
 * display.c - SH8601 AMOLED 显示驱动
 */

#include "display.h"
#include "pin_config.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "driver/spi_master.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "display";

static lv_display_t *lvgl_disp = NULL;
static esp_lcd_panel_io_handle_t panel_io = NULL;

// SH8601 命令
#define SH8601_CMD_SLPIN   0x10
#define SH8601_CMD_SLPOUT  0x11
#define SH8601_CMD_DISPON  0x29
#define SH8601_CMD_DISPOFF 0x28
#define SH8601_CMD_WRDISBV 0x51
#define SH8601_CMD_WRCTRLD 0x53

esp_err_t display_set_brightness(uint8_t brightness)
{
    if (!panel_io) return ESP_ERR_INVALID_STATE;
    return esp_lcd_panel_io_tx_param(panel_io, SH8601_CMD_WRDISBV, &brightness, 1);
}

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing display...");

    // SPI 总线 (QSPI)
    spi_bus_config_t bus_cfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        LCD_SCLK_PIN, LCD_SDIO0_PIN, LCD_SDIO1_PIN,
        LCD_SDIO2_PIN, LCD_SDIO3_PIN,
        LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t)
    );
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // LCD Panel IO
    esp_lcd_panel_io_spi_config_t io_cfg = SH8601_PANEL_IO_QSPI_CONFIG(
        LCD_CS_PIN, NULL, NULL
    );
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_cfg, &panel_io));

    // SH8601 Panel
    esp_lcd_panel_handle_t panel_handle = NULL;
    sh8601_vendor_config_t vendor_cfg = {
        .flags = {
            .use_qspi_interface = 1,
        },
    };

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_endian = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_cfg,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(panel_io, &panel_cfg, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // Sleep out
    esp_lcd_panel_io_tx_param(panel_io, SH8601_CMD_SLPOUT, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));

    // Display on
    esp_lcd_panel_disp_on_off(panel_handle, true);
    vTaskDelay(pdMS_TO_TICKS(20));

    // Enable backlight control and set brightness
    uint8_t ctrl_d = 0x24;  // Enable brightness control
    esp_lcd_panel_io_tx_param(panel_io, SH8601_CMD_WRCTRLD, &ctrl_d, 1);

    uint8_t brightness = 200;  // Bright (0-255)
    esp_lcd_panel_io_tx_param(panel_io, SH8601_CMD_WRDISBV, &brightness, 1);

    ESP_LOGI(TAG, "SH8601 initialized, brightness=%d", brightness);

    // LVGL Port
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // Add display
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = panel_io,
        .panel_handle = panel_handle,
        .buffer_size = LCD_WIDTH * 40,
        .double_buffer = true,
        .hres = LCD_WIDTH,
        .vres = LCD_HEIGHT,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,
        },
    };

    lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    if (!lvgl_disp) {
        ESP_LOGE(TAG, "Failed to add display to LVGL port");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Display initialized: %dx%d", LCD_WIDTH, LCD_HEIGHT);
    return ESP_OK;
}

lv_display_t *display_get(void)
{
    return lvgl_disp;
}

bool display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void display_unlock(void)
{
    lvgl_port_unlock();
}
