"""Text-to-speech via MiMo v2.5 streaming API."""

import base64
import json
import logging
import httpx
from .config import TTSConfig

logger = logging.getLogger("xz.tts")

VOICE_CN = "冰糖"


class TTSService:
    def __init__(self, config: TTSConfig):
        self.config = config
        self._client = httpx.AsyncClient(
            timeout=httpx.Timeout(120.0),
            headers={
                "api-key": config.api_key,
                "Content-Type": "application/json",
            },
        )

    async def close(self):
        await self._client.aclose()

    async def synthesize(self, text: str) -> bytes:
        """Non-streaming fallback. Returns PCM 24kHz 16-bit mono."""
        body = {
            "model": "mimo-v2.5-tts",
            "messages": [
                {"role": "user", "content": "用温柔阳光的姐姐语气，正常语速，像在和喜欢的弟弟聊天。"},
                {"role": "assistant", "content": text},
            ],
            "audio": {"format": "pcm16", "voice": VOICE_CN},
        }
        try:
            resp = await self._client.post(
                f"{self.config.base_url}/chat/completions",
                content=json.dumps(body, ensure_ascii=False),
            )
            if resp.status_code != 200:
                logger.error(f"TTS error: status={resp.status_code}")
                return b""
            result = resp.json()
            b64 = result["choices"][0]["message"]["audio"]["data"]
            return base64.b64decode(b64)
        except Exception as e:
            logger.error(f"TTS failed: {e}")
            return b""

    async def synthesize_stream(self, text: str):
        """Stream TTS audio chunks as (pcm_bytes, is_last) tuples.
        MiMo streaming currently in compatibility mode — returns one chunk.
        """
        body = {
            "model": "mimo-v2.5-tts",
            "messages": [
                {"role": "user", "content": "用温柔阳光的姐姐语气，正常语速，像在和喜欢的弟弟聊天。"},
                {"role": "assistant", "content": text},
            ],
            "audio": {"format": "pcm16", "voice": VOICE_CN},
            "stream": True,
        }

        try:
            async with httpx.AsyncClient(
                timeout=httpx.Timeout(120.0),
                headers={
                    "api-key": self.config.api_key,
                    "Content-Type": "application/json",
                },
            ) as client:
                async with client.stream(
                    "POST",
                    f"{self.config.base_url}/chat/completions",
                    content=json.dumps(body, ensure_ascii=False),
                ) as resp:
                    if resp.status_code != 200:
                        body_text = await resp.aread()
                        logger.error(f"TTS stream error: status={resp.status_code} body={body_text[:300]}")
                        return

                    async for line in resp.aiter_lines():
                        if not line.startswith("data:"):
                            continue
                        data_str = line[5:].strip()
                        if data_str == "[DONE]":
                            break
                        try:
                            event = json.loads(data_str)
                        except json.JSONDecodeError:
                            continue

                        # Try to extract audio from various locations
                        b64 = ""
                        try:
                            # Standard: choices[0].delta.audio.data
                            b64 = event["choices"][0]["delta"]["audio"]["data"]
                        except (KeyError, IndexError, TypeError):
                            try:
                                # Fallback: choices[0].message.audio.data
                                b64 = event["choices"][0]["message"]["audio"]["data"]
                            except (KeyError, IndexError, TypeError):
                                try:
                                    # Maybe in top-level?
                                    b64 = event.get("audio", {}).get("data", "")
                                except (KeyError, IndexError, TypeError, AttributeError):
                                    pass

                        if b64:
                            pcm = base64.b64decode(b64)
                            yield pcm, False

        except Exception as e:
            logger.error(f"TTS stream error: {e}")
            # Fall back to non-streaming
            pcm = await self.synthesize(text)
            if pcm:
                yield pcm, False

        yield b"", True
