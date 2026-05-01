# ESP32-S3-Touch-AMOLED-1.8 多应用系统

基于 Waveshare ESP32-S3-Touch-AMOLED-1.8 开发板的模块化多应用系统，使用 ESP-IDF + LVGL 构建。

## 硬件配置

| 功能 | 芯片/型号 | 接口 |
|------|-----------|------|
| 主控 | ESP32-S3R8（双核 240MHz，8MB PSRAM，16MB Flash） | — |
| 显示屏 | 1.8" AMOLED 368×448 | SH8601 QSPI |
| 触摸 | FT5x06 | I2C（地址 0x38） |
| 电池 | AXP2101 PMIC | I2C（地址 0x34） |
| IMU | QMI8658（6轴加速度+陀螺仪） | I2C（地址 0x6B） |
| 音频 | ES8311 编解码（麦克风+喇叭） | I2S |
| RTC | PCF85063 | I2C |
| 无线 | WiFi 2.4GHz + BLE 5.0 | 板载天线 |
| 扩展 | Micro SD 卡槽 | SDMMC |
| 按钮 | PWR（电源）、BOOT（GPIO0 自定义） | GPIO |

### 引脚分布

| 引脚 | 功能 |
|------|------|
| GPIO4 ~ GPIO7 | QSPI DATA0 ~ DATA3 |
| GPIO11 | QSPI CLK |
| GPIO12 | QSPI CS |
| GPIO14 | I2C SCL（触摸/传感器/音频共用） |
| GPIO15 | I2C SDA（触摸/传感器/音频共用） |
| GPIO21 | 触摸中断 INT |
| GPIO0 | BOOT 按钮 |

## 项目结构

```
├── main/
│   ├── main.c                  # 入口，系统初始化
│   ├── app_framework.c/h       # 应用注册表 + 导航状态机
│   ├── ui_home.c/h             # 主屏幕（时钟/电池/WiFi）
│   ├── ui_menu.c/h             # 3×3 应用菜单
│   ├── sys_display.c/h         # SH8601 显示屏驱动 + LVGL
│   ├── sys_touch.c/h           # FT5x06 触摸驱动
│   ├── sys_i2c.c/h             # I2C 总线 + TCA9554 电源管理
│   ├── sys_battery.c/h         # AXP2101 电池管理（规划中）
│   ├── sys_imu.c/h             # QMI8658 姿态传感器（规划中）
│   ├── sys_wifi.c/h            # WiFi + NTP 时间同步（规划中）
│   ├── sys_audio.c/h           # ES8311 音频（规划中）
│   ├── sys_sdcard.c/h          # SD 卡（规划中）
│   ├── sys_button.c/h          # 按键处理（规划中）
│   ├── sys_config.c/h          # NVS 配置存储（规划中）
│   ├── app_settings.c/h        # 设置应用（规划中）
│   ├── app_attitude.c/h        # 姿态仪（规划中）
│   ├── app_voice.c/h           # 语音助手（规划中）
│   ├── app_music.c/h           # 音乐播放器（规划中）
│   ├── app_metronome.c/h       # 节拍器（规划中）
│   ├── app_weather.c/h         # 天气显示（规划中）
│   ├── app_pedometer.c/h       # 计步器（规划中）
│   └── app_ball.c/h            # 重力小球（规划中）
├── components/                 # 本地组件
│   ├── esp_lcd_sh8601/         # SH8601 LCD 面板驱动
│   ├── espressif__esp_lcd_touch/         # ESP 触摸抽象层
│   ├── espressif__esp_lcd_touch_ft5x06/  # FT5x06 触摸驱动
│   └── espressif__cmake_utilities/       # CMake 工具
├── managed_components/         # 托管组件（自动下载）
│   ├── lvgl__lvgl/             # LVGL 8.4.0
│   ├── espressif__esp_io_expander/         # IO 扩展抽象层
│   └── espressif__esp_io_expander_tca9554/ # TCA9554 驱动
├── CMakeLists.txt
├── partitions.csv              # 分区表
├── sdkconfig / sdkconfig.defaults
├── build_flash.bat             # Windows 一键编译烧写脚本
└── .gitignore
```

## 开发环境

- **ESP-IDF**: v5.4
- **LVGL**: 8.4.0
- **编译工具链**: xtensa-esp-elf 14.2.0
- **烧写工具**: esptool.py v4.8.1
- **串口**: COM9（460800 bps）

## 编译 & 烧写

### Windows

双击 `build_flash.bat`，或命令行运行：

```bat
build_flash.bat
```

### 手动编译

```bash
# 设置目标芯片
idf.py set-target esp32s3

# 编译
idf.py build

# 烧写（COM9 替换为实际串口）
idf.py -p COM9 flash

# 查看日志
idf.py -p COM9 monitor
```

## 已实现（Phase 1）

- [x] 模块化项目结构，每个应用独立 .c 文件
- [x] SH8601 AMOLED 显示驱动（QSPI，368×448）
- [x] FT5x06 电容触摸输入
- [x] LVGL 8.4 双缓冲渲染
- [x] 主屏幕：时钟、日期、电池/WiFi 占位
- [x] 3×3 应用图标菜单
- [x] 应用框架：手势导航（上滑进入菜单，下滑返回主页）
- [x] 应用注册表机制，可扩展新应用

## 开发中

| 阶段 | 内容 |
|------|------|
| Phase 2 | AXP2101 电池、QMI8658 IMU、WiFi+NTP、按键、NVS、SD 卡 |
| Phase 3 | 设置应用、姿态仪 |
| Phase 4 | 天气显示、计步器 |
| Phase 5 | 重力小球物理模拟 |
| Phase 6 | ES8311 音频、音乐播放器、节拍器 |
| Phase 7 | 语音助手（DeepSeek LLM + MIMO TTS） |

## 应用导航

| 操作 | 效果 |
|------|------|
| 开机 | 显示主屏幕 |
| 主屏向上滑动 | 进入应用菜单 |
| 菜单点击图标 | 打开应用 |
| 应用内点返回 | 回到菜单 |
| 菜单向下滑动 | 回到主屏 |

## 技术选型

| 项目 | 选择 |
|------|------|
| LLM API | DeepSeek |
| TTS 引擎 | MIMO TTS |
| 天气 API | 和风天气 (QWeather) |
| 音频格式 | WAV |
