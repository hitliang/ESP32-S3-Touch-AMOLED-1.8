# ESP32-S3-Touch-AMOLED-1.8 多应用系统

基于 Waveshare ESP32-S3-Touch-AMOLED-1.8 开发板的模块化多应用系统，使用 ESP-IDF v5.4 + LVGL 8.4 构建。

## 硬件配置

| 功能 | 芯片/型号 | 接口 | 引脚 |
|------|-----------|------|------|
| 主控 | ESP32-S3R8（双核 240MHz，8MB PSRAM，16MB Flash） | — | — |
| 显示屏 | 1.8" AMOLED 368×448 | SH8601 QSPI | CLK=11, D0-3=4-7, CS=12 |
| 触摸 | FT5x06 电容触摸 | I2C（地址 0x38） | SCL=14, SDA=15, INT=21 |
| 电池 | AXP2101 PMIC | I2C（地址 0x34） | SCL=14, SDA=15 |
| IMU | QMI8658（6轴加速度+陀螺仪） | I2C（地址 0x6B） | SCL=14, SDA=15 |
| 音频 | ES8311 编解码（麦克风+喇叭） | I2S | MCK=16, BCK=9, WS=45, DO=8 |
| RTC | PCF85063 | I2C | SCL=14, SDA=15 |
| 无线 | WiFi 2.4GHz + BLE 5.0 | 板载天线 | — |
| 扩展 | Micro SD 卡槽 | SDMMC 1-bit | — |
| 按钮 | BOOT（GPIO0） | GPIO | 短按=开关屏/返回主页 |
| 功放 | PA 使能 | GPIO | GPIO46 |

## 项目结构

```
main/
  main.c                  # 入口，系统初始化顺序
  app_framework.c/h       # 应用注册表 + 导航状态机
  ui_home.c/h             # 主屏幕（时钟/电池/WiFi）
  ui_menu.c/h             # 3×3 应用菜单
  sys_i2c.c/h             # I2C 总线 + TCA9554 电源管理
  sys_display.c/h         # SH8601 显示屏驱动 + LVGL
  sys_touch.c/h           # FT5x06 触摸驱动
  sys_battery.c/h         # AXP2101 电池检测
  sys_imu.c/h             # QMI8658 IMU 驱动
  sys_wifi.c/h            # WiFi STA + NTP 时间同步
  sys_audio.c/h           # ES8311 音频播放
  sys_button.c/h          # BOOT 按键处理
  sys_config.c/h          # NVS 配置存储
  sys_sdcard.c/h          # SD 卡驱动
  audio_test.c/h          # 音频测试功能
  app_settings.c/h        # 设置应用
  app_attitude.c/h        # 姿态仪
  app_weather.c/h         # 天气显示
  app_voice.c/h           # 语音助手（LLM + TTS）
  app_ball.c/h            # 重力小球物理模拟
  app_snake.c/h           # 重力贪吃蛇
  secrets.h               # API 密钥（不上传 Git）
components/               # 本地组件
managed_components/       # 托管组件（LVGL、ES8311、IO扩展等）
build_flash.bat           # 一键编译烧写
DEVELOPMENT.md            # 开发操作手册
```

## 开发环境

- **ESP-IDF**: v5.4
- **LVGL**: 8.4.0（含 SIMSUN 16 CJK 中文字库）
- **编译工具链**: xtensa-esp-elf 14.2.0
- **烧写工具**: esptool.py v4.8.1
- **串口**: COM9（460800 bps）

## 编译 & 烧写

```bat
cd D:\ESP32-S3-Touch-AMOLED-1.8-main
build_flash.bat
```

或手动：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COM9 flash
```

## 已实现功能

### 系统框架
- 模块化架构，每个应用独立 .c/.h 文件
- 应用注册表机制，新增应用只需 3 步
- 导航状态机：HOME ↔ MENU → APP
- 滑动进菜单、点击进应用、BOOT 键回主页
- AMOLED 纯黑背景（真黑像素关闭）
- SIMSUN 16 CJK 中文字库支持
- SD 卡对话记忆持久化（插卡自动保存/加载）

### 主屏幕
- 48pt 大时钟 + 青色发光效果
- 24pt 日期 + 星期
- 实时电池 %（AXP2101，颜色随电量变化）
- WiFi 状态指示（绿/黄/红）
- BOOT 键关屏/开屏

### 应用列表

| 应用 | 功能 | 状态 |
|------|------|------|
| Settings | WiFi 状态、电池、固件信息 | 完成 |
| Attitude | Roll/Pitch 弧表 + 气泡水平仪（IMU 实时） | 完成 |
| Weather | 高德 API 实时天气 + 3 天预报 | 完成 |
| Voice AI | DeepSeek LLM 对话 + SIMSUN 中文显示 | 完成 |
| Ball Physics | 7 个重力小球 + 碰撞检测（IMU 操控） | 完成 |
| Snake | 重力贪吃蛇（IMU 操控方向） | 完成 |
| Voice AI（TTS） | LLM 回复通过 MiMo TTS 朗读 | 调试中 |
| Pedometer | 计步器 | 规划中 |
| Music Player | 音乐播放器 | 规划中 |
| Metronome | 节拍器 | 规划中 |

## 操作说明

| 操作 | 效果 |
|------|------|
| 开机 | 显示主屏幕 |
| 主屏向上滑动 | 进入应用菜单 |
| 菜单点击图标 | 打开应用 |
| BOOT 键（应用/菜单内） | 返回主屏 |
| BOOT 键（主屏） | 关屏/开屏 |
| 主屏 Sound 按钮 | 测试音频（440Hz） |
| 菜单向下滑动 | 返回主屏 |

## 技术选型

| 项目 | 选择 |
|------|------|
| LLM API | DeepSeek (deepseek-chat) |
| TTS 引擎 | MiMo TTS (Chloe 音色) |
| 天气 API | 高德天气 API |
| 音频格式 | WAV |
| 中文字体 | SIMSUN 16 CJK |
| API Key 管理 | secrets.h（不上传 Git） |
