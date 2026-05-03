# 开发操作手册

## 一键编译烧写（PowerShell）

以管理员身份打开 PowerShell，执行：

```powershell
$env:IDF_PATH = "D:\program\idf\v5.4\esp-idf"
$env:PATH = "C:\Users\reddy\.espressif\python_env\idf5.4_py3.11_env\Scripts;D:\program\idf\v5.4\esp-idf\tools;C:\Users\reddy\.espressif\tools\cmake\3.30.2\bin;C:\Users\reddy\.espressif\tools\ninja\1.12.1;C:\Users\reddy\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;C:\Users\reddy\.espressif\tools\riscv32-esp-elf\esp-14.2.0_20241119\riscv32-esp-elf\bin;C:\Users\reddy\.espressif\tools\esp-rom-elfs\20241011;C:\Users\reddy\.espressif\tools\idf-git\2.39.2\cmd;" + $env:PATH
cd D:\ESP32-S3-Touch-AMOLED-1.8-main
python "$env:IDF_PATH\tools\idf.py" build
python "$env:IDF_PATH\tools\idf.py" flash -p COM9
```

## sdkconfig 变了需要重新配置（PowerShell）

```powershell
Remove-Item -Recurse -Force build
# 然后重新运行上面的编译烧写命令
```

## 代码中有新文件需要添加到编译

编辑 `main\CMakeLists.txt`，把新的 `.c` 文件名加到 `SRCS` 列表里。

## 新增应用

1. 创建 `main\app_xxx.c` 和 `main\app_xxx.h`
2. 在 `app_xxx.c` 里定义 `const app_entry_t app_xxx = {...}` 结构体
3. 在 `main\app_framework.c` 最上面加 `#include "app_xxx.h"`
4. 在 `app_framework.c` 的 `app_framework_init()` 里加 `app_registry[idx++] = APP(xxx);`
5. 在 `main\CMakeLists.txt` 的 SRCS 里加 `"app_xxx.c"`
6. 如果想更新菜单图标，改 `main\ui_menu.c` 的 `app_icons[]` 数组

## API Key 管理

所有密钥在 `main\secrets.h`，**此文件不上传 GitHub**（已在 `.gitignore`）。

## 提交推送

```bat
cd D:\ESP32-S3-Touch-AMOLED-1.8-main
git add -A
git commit -m "你的提交信息"
git push
```

注意：提交信息用中文，不要 force push。

## 项目文件结构

```
main/
  main.c                  # 入口，系统初始化顺序
  app_framework.c/h       # 应用注册表 + 导航
  ui_home.c/h             # 主屏幕
  ui_menu.c/h             # 3x3 菜单
  sys_i2c.c/h             # I2C 总线
  sys_display.c/h         # 显示 + LVGL
  sys_touch.c/h           # 触摸（含滑动检测）
  sys_battery.c/h         # AXP2101 电池
  sys_imu.c/h             # QMI8658 姿态传感器
  sys_wifi.c/h            # WiFi + NTP
  sys_audio.c/h           # ES8311 音频
  sys_button.c/h          # BOOT 按键
  sys_config.c/h          # NVS 配置
  sys_sdcard.c/h          # SD 卡
  audio_test.c/h          # 音频测试按钮
  app_settings.c/h        # 设置应用
  app_attitude.c/h        # 姿态仪
  app_weather.c/h         # 天气（高德 API）
  app_voice.c/h           # 语音助手（DeepSeek + MiMo TTS）
  app_ball.c/h            # 重力小球
  app_snake.c/h           # 贪吃蛇
  secrets.h               # API 密钥（不上传）
```

## 调试技巧

1. 加 `printf("DEBUG: xxx\n");` 打印日志
2. 串口监控：`python C:\Users\reddy\read_serial.py`
3. 常见编译错误：
   - `unused function` → 删掉不用函数，或注释掉
   - `misleading indentation` → if 语句分行写，加花括号
   - `implicit declaration` → 忘记 `#include` 头文件
