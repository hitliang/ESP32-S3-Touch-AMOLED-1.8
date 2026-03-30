/*
 * pin_config.h - ESP32-S3-Touch-AMOLED-1.8 引脚定义
 */

#pragma once

// ==================== 显示屏 (SH8601 QSPI) ====================
#define LCD_CS_PIN      12
#define LCD_SCLK_PIN    11
#define LCD_SDIO0_PIN   4
#define LCD_SDIO1_PIN   5
#define LCD_SDIO2_PIN   6
#define LCD_SDIO3_PIN   7

// ==================== 触摸 (FT3168 I2C) ====================
#define I2C_SDA_PIN     15
#define I2C_SCL_PIN     14
#define TP_INT_PIN      21

// ==================== 音频 (ES8311 I2S) ====================
#define I2S_MCK_PIN     16
#define I2S_BCK_PIN     9
#define I2S_WS_PIN      45
#define I2S_DO_PIN      10
#define I2S_DI_PIN      8
#define PA_EN_PIN       46

// ==================== I2C 地址 ====================
#define FT3168_ADDR     0x38
#define QMI8658_ADDR    0x6B
#define PCF85063_ADDR   0x51
#define AXP2101_ADDR    0x34
#define TCA9554_ADDR    0x20

// ==================== TCA9554 扩展引脚 ====================
#define TCA_LCD_RESET       0
#define TCA_LCD_PWR_EN      1
#define TCA_TOUCH_RESET     2
#define TCA_RTC_IRQ         3
#define TCA_PWR_BUTTON      4
#define TCA_AXP_IRQ         5

// ==================== 屏幕参数 ====================
#define LCD_WIDTH       368
#define LCD_HEIGHT      448
#define LCD_CMD_BITS    8
#define LCD_PARAM_BITS  8
