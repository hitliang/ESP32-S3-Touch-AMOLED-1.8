# ESP32-S3-Touch-AMOLED-1.8 硬件规格

本文档记录开发板的详细硬件参数、引脚定义和外设说明。详见 [微雪 Wiki](https://www.waveshare.net/wiki/ESP32-S3-Touch-AMOLED-1.8)。

---

## 板载组件清单

| # | 组件 | 说明 |
|---|---|---|
| 1 | ESP32-S3R8 | Wi-Fi 和蓝牙 SoC，240MHz 双核，叠封 8MB PSRAM |
| 2 | QMI8658 | 六轴 IMU（3 轴陀螺仪 + 3 轴加速度计） |
| 3 | PCF85063 | RTC 时钟芯片 |
| 4 | AXP2101 | 高集成度电源管理芯片 |
| 5 | ES8311 | 低功耗音频编解码芯片 |
| 6 | 扬声器 | 板载扬声器 |
| 7 | 备用电池焊盘 | 更换主电池时维持 RTC 供电 |
| 8 | MX1.25 锂电池接口 | 3.7V 电池充放电，推荐 3.85 x 24 x 28mm 规格 |
| 9 | 16MB NOR-Flash | 数据存储 |
| 10 | 麦克风 | 音频采集 |
| 11 | 贴片天线 | 2.4GHz Wi-Fi + Bluetooth 5 (LE) |
| 12 | GPIO 焊盘 | 1mm 间距，引出可用 IO |
| 13 | BOOT 按键 | 设备启动和功能调试 |
| 14 | Type-C 接口 | USB 烧录和日志打印 |
| 15 | PWR 电源按键 | 控制电源通断，支持自定义功能 |

---

## 引脚定义

### 显示屏 (SH8601 - QSPI)

| 信号 | GPIO | 说明 |
|---|---|---|
| LCD_CS | 12 | 片选 |
| LCD_SCLK | 11 | 时钟 |
| LCD_SDIO0 | 4 | 数据线 0 |
| LCD_SDIO1 | 5 | 数据线 1 |
| LCD_SDIO2 | 6 | 数据线 2 |
| LCD_SDIO3 | 7 | 数据线 3 |

- 分辨率：368 x 448
- 色彩：16.7M（24-bit）
- 驱动芯片：SH8601

### 触摸屏 (FT3168 - I2C)

| 信号 | GPIO | 说明 |
|---|---|---|
| IIC_SDA | 15 | I2C 数据 |
| IIC_SCL | 14 | I2C 时钟 |
| TP_INT | 21 | 触摸中断 |

- 驱动芯片：FT3168 / FT6146
- 通信速率：10KHz ~ 400KHz

### 音频 (ES8311 - I2S)

| 信号 | GPIO | 说明 |
|---|---|---|
| I2S_MCK | 16 | 主时钟 |
| I2S_BCK | 9 | 位时钟 |
| I2S_WS | 45 | 字选择 |
| I2S_DO | 10 | 数据输出（DAC -> 扬声器） |
| I2S_DI | 8 | 数据输入（麦克风 -> ADC） |
| PA | 46 | 功放使能 |

### SD 卡

| 信号 | GPIO | 说明 |
|---|---|---|
| SDMMC_CMD | 38 | SD 卡命令线 |
| SDMMC_CLK | 39 | SD 卡时钟线 |
| SDMMC_D0 | 40 | SD 卡数据线 0 |
| SDMMC_D1 | 41 | SD 卡数据线 1 |
| SDMMC_D2 | 42 | SD 卡数据线 2 |
| SDMMC_D3 | 47 | SD 卡数据线 3 |

### TCA9554 GPIO 扩展器 (I2C)

通过 I2C 扩展的 GPIO，用于控制板上外设：

| 扩展引脚 | 功能 | 说明 |
|---|---|---|
| P0 | LCD Reset | 显示屏复位 |
| P1 | LCD Power Enable | 显示屏电源使能 |
| P2 | Touch Reset | 触摸芯片复位 |
| P3 | RTC IRQ | RTC 中断输出 |
| P4 | PWR Button | 电源按键状态读取 |
| P5 | AXP IRQ | 电源管理芯片中断 |

- I2C 地址：0x20（TCA9554）
- 共享 I2C 总线（SDA: GPIO15, SCL: GPIO14）

### I2C 设备地址总览

| 设备 | I2C 地址 | 说明 |
|---|---|---|
| FT3168 | 0x38 | 触摸控制器 |
| QMI8658 | 0x6B | 六轴 IMU |
| PCF85063 | 0x51 | RTC 时钟 |
| AXP2101 | 0x34 | 电源管理 |
| TCA9554 | 0x20 | GPIO 扩展器 |

### 按键

| 按键 | GPIO | 说明 |
|---|---|---|
| BOOT | GPIO0 | 启动/调试按键 |
| PWR | 通过 TCA9554 P4 | 电源按键（长按 6 秒关机） |

### GPIO 焊盘（预留扩展）

引出 7 个 GPIO + 1 路 I2C + 1 路 UART + 1 路 USB 焊盘，具体引脚见 Wiki 引脚图。

---

## 存储

| 类型 | 容量 | 说明 |
|---|---|---|
| SRAM | 512KB | 内置 |
| PSRAM | 8MB | 叠封（OPI） |
| Flash | 16MB | 外接 NOR-Flash |
| SD 卡 | Micro SD | 扩展存储 |

## 电源管理 (AXP2101)

- 支持 3.7V 锂电池充放电
- 多路可配置电压输出
- 集成充电与电池管理
- RTC 备用电池支持

## Arduino 开发板配置

| 配置项 | 值 |
|---|---|
| 开发板 | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| Flash Size | 16MB |
| PSRAM | OPI |
| Partition Scheme | 16MB (3MB APP / 9.9MB FATFS) |
| ESP32 板管理 | >=3.0.6 |

## 依赖库

| 库 | 版本 | 说明 | 安装方式 |
|---|---|---|---|
| Arduino_DriveBus | — | FT3168 触摸驱动 | 离线 |
| GFX_Library_for_Arduino | v1.4.9 | SH8601 图形库 | 离线 |
| ESP32_IO_Expander | v0.0.3 | TCA9554 驱动 | 在线/离线 |
| LVGL | v8.4.0 | 图形化 UI 库 | 离线（需复制 demos） |
| SensorLib | v0.2.1 | PCF85063 / QMI8658 | 在线/离线 |
| XPowersLib | v0.2.6 | AXP2101 电源管理 | 在线/离线 |
