# ESP32-S3-Touch-AMOLED-1.8 多应用系统

基于 Waveshare ESP32-S3-Touch-AMOLED-1.8 开发板的模块化多应用系统，使用 ESP-IDF v5.4 + LVGL 8.4 构建。

语音助手采用 **设备端瘦客户端 + 服务端 Agent** 架构：设备通过 WebSocket 推送 Opus 音频流，服务端完成 STT → LLM Agent → TTS 全链路处理，设备端只负责录音和播放。

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

## 语音助手架构

```
┌─────────────────────┐         WebSocket          ┌────────────────────────────┐
│   ESP32-S3 设备端    │◄────── Opus 音频流 ──────►│    Python 服务端             │
│   (app_xiaozhi.c)    │                           │    (server/xiaozhi/)        │
│                      │  binary protocol v3       │                            │
│   录音 → Opus编码 → 发送 ────────────────────►  Opus解码 → STT (阿里云NLS)    │
│                      │                           │              ↓             │
│   播放 ← Opus解码 ← 接收 ◄────────────────────  Opus编码 ← TTS (MiMo v2.5)   │
│                      │                           │              ↓             │
│   对话显示 ← JSON ← 接收 ◄───────────────────  Agent (DeepSeek)              │
│                      │                           │              ↓             │
│                      │                           │   四层记忆 + 工具系统 + 管理页 │
└─────────────────────┘                           └────────────────────────────┘
```

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
  sys_audio.c/h           # ES8311 音频播放/录制
  sys_button.c/h          # BOOT 按键处理
  sys_config.c/h          # NVS 配置存储
  sys_sdcard.c/h          # SD 卡驱动
  xiaozhi_client.c/h      # xiaozhi 协议栈（WebSocket + Opus）
  app_xiaozhi.c/h         # 语音助手应用
  app_settings.c/h        # 设置应用
  app_attitude.c/h        # 姿态仪
  app_weather.c/h         # 天气显示
  app_ball.c/h            # 重力小球物理模拟
  app_snake.c/h           # 重力贪吃蛇
  audio_test.c/h          # 音频测试
  secrets.h               # API 密钥（不上传 Git）
components/               # 本地组件
managed_components/       # 托管组件（LVGL、ES8311等）
build_flash.bat           # 一键编译烧写

server/                   # 服务端
  xiaozhi/
    server.py             # WebSocket 服务（VAD + 音频流管理）
    agent.py              # LLM Agent（ReAct + 工具调用）
    memory.py             # 四层记忆系统
    protocol.py           # xiaozhi 二进制协议 v3
    codec.py              # Opus 编解码
    stt.py                # 阿里云 NLS 语音识别
    tts.py                # MiMo v2.5 流式 TTS
    config.py             # 配置管理
    admin.py              # 管理页面（对话记录查看）
    run.py                # 入口
  test_xiaozhi_client.py  # PC 端测试客户端
  requirements.txt
  README.md
```

## 开发环境

- **ESP-IDF**: v5.4
- **LVGL**: 8.4.0（含 SIMSUN 16 CJK 中文字库）

## 编译 & 烧写

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COM9 flash
```

## 服务端部署

见 [server/README.md](server/README.md)。

设备端通过 `XZ_WS_URL` 宏指向服务端地址，无需修改其他代码。

## 已实现功能

### 系统框架
- 模块化架构，每个应用独立 .c/.h 文件
- 应用注册表机制，新增应用只需 3 步
- 导航状态机：HOME ↔ MENU → APP
- 滑动进菜单、点击进应用、BOOT 键回主页
- AMOLED 纯黑背景（真黑像素关闭）
- SIMSUN 16 CJK 中文字库支持

### 主屏幕
- 48pt 大时钟 + 青色发光效果
- 24pt 日期 + 星期
- 实时电池 %（AXP2101，颜色随电量变化）
- WiFi 状态指示（绿/黄/红）
- BOOT 键关屏/开屏

### 应用列表

| 应用 | 功能 | 状态 |
|------|------|------|
| **XiaoZhi AI** | WebSocket 语音助手，打开即用，自动录音 | 完成 |
| Settings | WiFi 状态、电池、固件信息 | 完成 |
| Attitude | Roll/Pitch 弧表 + 气泡水平仪（IMU 实时） | 完成 |
| Weather | 高德 API 实时天气 + 3 天预报 | 完成 |
| Ball Physics | 7 个重力小球 + 碰撞检测（IMU 操控） | 完成 |
| Snake | 重力贪吃蛇（IMU 操控方向） | 完成 |
| Audio Test | 440Hz 测试音 | 完成 |
| Pedometer | 计步器 | 规划中 |
| Music Player | 音乐播放器 | 规划中 |
| Metronome | 节拍器 | 规划中 |

### 服务端功能
- WebSocket 服务 + xiaozhi 二进制协议 v3
- 阿里云 NLS 实时语音识别（STT）
- DeepSeek LLM Agent（ReAct + 工具调用）
- MiMo v2.5 流式语音合成（TTS）
- 四层记忆系统（近期对话 + 会话摘要 + 长期事实 + 用户画像）
- 工具系统（计算器等，可扩展）
- 管理页面：对话记录分页查看

## 操作说明

| 操作 | 效果 |
|------|------|
| 开机 | 显示主屏幕 |
| 主屏向上滑动 | 进入应用菜单 |
| 菜单点击图标 | 打开应用 |
| XiaoZhi AI 打开 | 自动连接服务器，开始录音 |
| XiaoZhi 内点按钮 | 挂断/重连 |
| BOOT 键（应用/菜单内） | 返回主屏 |
| BOOT 键（主屏） | 关屏/开屏 |
| 主屏 Sound 按钮 | 测试音频（440Hz） |
| 菜单向下滑动 | 返回主屏 |

## 技术选型

| 项目 | 选择 |
|------|------|
| 设备端协议 | xiaozhi 二进制协议 v3（WebSocket + Opus 60ms） |
| LLM | DeepSeek (deepseek-chat)，131K max_tokens |
| STT | 阿里云 NLS 实时语音识别 |
| TTS | MiMo v2.5，音色"冰糖" |
| 记忆 | 四层架构（SQLite 持久化，自动摘要+事实提取+画像更新） |
| 天气 API | 高德天气 API |
| 中文字体 | SIMSUN 16 CJK |
