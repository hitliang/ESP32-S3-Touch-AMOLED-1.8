# ESP32-S3-Touch-AMOLED-1.8

基于 [微雪 ESP32-S3-Touch-AMOLED-1.8](https://www.waveshare.net/wiki/ESP32-S3-Touch-AMOLED-1.8) 开发板的多功能项目。

## 硬件平台

| 参数 | 规格 |
|---|---|
| 芯片 | ESP32-S3R8 双核 LX7, 240MHz |
| 内存 | 512KB SRAM + 8MB PSRAM |
| 存储 | 16MB Flash + Micro SD |
| 屏幕 | 1.8" AMOLED 触摸屏, 368×448, 16.7M 色 |
| 屏驱 | SH8601 (QSPI) |
| 触控 | FT3168 (I2C) |
| 传感器 | QMI8658 六轴 IMU |
| 音频 | ES8311 编解码 + 麦克风 + 扬声器 |
| 电源 | AXP2101 电源管理 + RTC (PCF85063) |
| 无线 | Wi-Fi 2.4GHz + Bluetooth 5 (LE) |
| 接口 | Type-C, GPIO 焊盘, I2C, UART, USB, SD 卡槽, MX1.25 喇叭接口 |

## 功能规划

- [ ] 基础显示驱动 & UI 框架
- [ ] 触摸交互系统
- [ ] Wi-Fi 联网
- [ ] 蓝牙功能
- [ ] 传感器数据采集 (IMU)
- [ ] 音频录制 & 播放
- [ ] RTC 时钟 & 电池管理
- [ ] SD 卡存储
- [ ] 更多功能开发中...

## 开发环境

- **Arduino IDE** (ESP32 板管理 ≥3.0.5)
- **ESP-IDF** (可选)

## 许可证

[MIT](LICENSE)
