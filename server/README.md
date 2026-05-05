# Reddy Bot — Xiaozhi 服务端

基于 ESP32-S3-Touch-AMOLED-1.8 设备的语音助手服务端，实现 xiaozhi 二进制协议 v3，提供完整的 STT → LLM Agent → TTS 流水线。

## 架构

```
ESP32 设备 ←→ WebSocket (7070) ←→ Python 服务端
                                      │
                               ┌──────┼──────┐
                               ▼      ▼      ▼
                             STT    LLM    TTS
                           (阿里云) (DeepSeek) (MiMo v2.5)
                                      │
                               ┌──────┴──────┐
                               ▼             ▼
                            记忆系统      工具系统
                         (4层记忆+SQLite)  (计算器等)
```

## 功能

- **WebSocket 服务**：实现 xiaozhi 二进制协议 v3（Opus 60ms/16kHz 音频帧 + JSON 控制消息）
- **STT 语音识别**：阿里云 NLS 实时语音识别（WebSocket 协议）
- **LLM Agent**：DeepSeek Chat，ReAct 循环 + 工具调用
- **TTS 语音合成**：MiMo v2.5 流式合成，音色"冰糖"
- **四层记忆**：近期对话 + 会话摘要 + 长期事实 + 用户画像
- **工具系统**：内置计算器等可扩展工具
- **管理页面**：端口 7071，对话记录分页查看

## 部署

### 环境要求

- Python 3.8+
- libopus
- 阿里云 NLS 项目（STT）
- DeepSeek API Key（LLM）
- MiMo API Key（TTS）

### 安装

```bash
cd /root/reddy_bot
python3.8 -m venv venv_xiaozhi
./venv_xiaozhi/bin/pip install -r requirements.txt
```

### 配置

编辑 `.env` 文件：

```bash
# STT — 阿里云 NLS
STT_PROVIDER=aliyun
ALIYUN_AK_ID=LTAIxxx
ALIYUN_AK_SECRET=xxx
ALIYUN_NLS_APPKEY=xxx

# LLM — DeepSeek
DEEPSEEK_API_KEY=sk-xxx
LLM_BASE_URL=https://api.deepseek.com/v1
LLM_MODEL=deepseek-chat

# TTS — MiMo v2.5
TTS_API_KEY=xxx

# 服务端口
XZ_HOST=0.0.0.0
XZ_PORT=7070
```

### 启动

```bash
systemctl start reddy-xiaozhi     # 启动
systemctl status reddy-xiaozhi    # 状态
journalctl -u reddy-xiaozhi -f    # 实时日志
```

### 管理页面

浏览器访问 `http://<服务器IP>:7071`，分页查看对话记录。

## 设备端配置

在 ESP32 固件的 `secrets.h` 或 `app_xiaozhi.c` 中设置：

```c
#define XZ_WS_URL "ws://<服务器IP>:7070"
```

设备将跳过官方 OTA 服务器直连此服务。

## 协议

### 二进制帧（v3）

```
┌─────────┬──────────┬───────────────┬──────────┐
│ type(1) │ resv(1)  │ size(2, BE)   │ payload  │
└─────────┴──────────┴───────────────┴──────────┘
  type: 0=Opus, 1=JSON
```

### 音频参数

- 编码：Opus 32kbps VoIP
- 采样率：16000 Hz
- 帧长：60ms（960 samples）
- 通道：单声道

## 工具系统

在 `server.py` 的 `_register_tools()` 中注册新工具：

```python
self.tools.register(
    {
        "type": "function",
        "function": {
            "name": "tool_name",
            "description": "工具描述",
            "parameters": {
                "type": "object",
                "properties": {
                    "arg1": {"type": "string", "description": "参数1"}
                },
                "required": ["arg1"],
            },
        },
    },
    handler_function,
)
```

已内置工具：`calculator` — 安全数学表达式求值。

## 项目结构

```
server/
├── xiaozhi/
│   ├── run.py         # 入口
│   ├── server.py      # WebSocket 服务 + VAD + 工具注册
│   ├── agent.py       # LLM Agent（ReAct + 工具调用）
│   ├── memory.py      # 四层记忆系统
│   ├── protocol.py    # xiaozhi 二进制协议 v3
│   ├── codec.py       # Opus 编解码
│   ├── stt.py         # 阿里云 NLS 语音识别
│   ├── tts.py         # MiMo v2.5 流式 TTS
│   ├── config.py      # 配置管理
│   └── admin.py       # 管理页面 HTTP 服务
├── test_xiaozhi_client.py  # PC 端测试客户端
├── requirements.txt
└── .env.example
```
