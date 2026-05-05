"""Configuration from environment variables."""

import os
from dataclasses import dataclass, field


@dataclass
class LLMConfig:
    api_key: str = ""
    base_url: str = "https://api.deepseek.com/v1"
    model: str = "deepseek-chat"
    max_tokens: int = 4096
    temperature: float = 0.7


@dataclass
class STTConfig:
    provider: str = "aliyun"  # aliyun or openai
    access_key_id: str = ""
    access_key_secret: str = ""
    appkey: str = ""


@dataclass
class TTSConfig:
    provider: str = "mimo"
    api_key: str = ""
    base_url: str = "https://api.xiaomimimo.com/v1"
    model: str = "mimo-v2-tts"
    voice: str = "mimo_default"


@dataclass
class MemoryConfig:
    db_path: str = "./data/reddy_memory.db"


@dataclass
class ServerConfig:
    host: str = "0.0.0.0"
    port: int = 7070


@dataclass
class Config:
    server: ServerConfig = field(default_factory=ServerConfig)
    llm: LLMConfig = field(default_factory=LLMConfig)
    stt: STTConfig = field(default_factory=STTConfig)
    tts: TTSConfig = field(default_factory=TTSConfig)
    memory: MemoryConfig = field(default_factory=MemoryConfig)
    system_prompt: str = ""
    log_level: str = "INFO"


def load_config() -> Config:
    cfg = Config()

    cfg.server.host = os.getenv("XZ_HOST", "0.0.0.0")
    cfg.server.port = int(os.getenv("XZ_PORT", "7070"))

    cfg.llm.api_key = os.getenv("DEEPSEEK_API_KEY", os.getenv("LLM_API_KEY", ""))
    cfg.llm.base_url = os.getenv("LLM_BASE_URL", "https://api.deepseek.com/v1")
    cfg.llm.model = os.getenv("LLM_MODEL", "deepseek-chat")

    cfg.stt.provider = os.getenv("STT_PROVIDER", "aliyun")
    cfg.stt.access_key_id = os.getenv("ALIYUN_AK_ID", "")
    cfg.stt.access_key_secret = os.getenv("ALIYUN_AK_SECRET", "")
    cfg.stt.appkey = os.getenv("ALIYUN_NLS_APPKEY", "")

    cfg.tts.api_key = os.getenv("TTS_API_KEY", os.getenv("MIMO_API_KEY", ""))
    cfg.tts.base_url = os.getenv("TTS_BASE_URL", "https://api.xiaomimimo.com/v1")

    cfg.system_prompt = os.getenv("SYSTEM_PROMPT", "")

    return cfg
