"""Text-to-speech via MiMo API."""

import io
import wave
import base64
import json
import logging
import httpx
from .config import TTSConfig

logger = logging.getLogger("xz.tts")


class TTSService:
    def __init__(self, config: TTSConfig):
        self.config = config
        self._client = httpx.AsyncClient(
            timeout=httpx.Timeout(120.0),
            headers={
                "Authorization": f"Bearer {config.api_key}",
                "Content-Type": "application/json",
            },
        )

    async def close(self):
        await self._client.aclose()

    async def synthesize(self, text: str) -> bytes:
        """Convert text to PCM bytes (16-bit, 24kHz mono).
        Returns empty bytes on failure.
        """
        body = {
            "model": self.config.model,
            "messages": [
                {"role": "user", "content": "please speak"},
                {"role": "assistant", "content": text},
            ],
            "audio": {
                "format": "wav",
                "voice": self.config.voice,
            },
        }

        try:
            resp = await self._client.post(
                f"{self.config.base_url}/chat/completions",
                content=json.dumps(body, ensure_ascii=False),
            )
            if resp.status_code != 200:
                logger.error(f"TTS error: status={resp.status_code} body={resp.text[:200]}")
                return b""

            # MiMo returns base64 WAV in the "data" field
            result = resp.json()
            raw = json.dumps(result)
            b64 = self._extract_b64(raw)
            if not b64:
                logger.error("TTS: no audio data found")
                return b""

            wav = base64.b64decode(b64)
            return self._wav_to_pcm(wav)
        except Exception as e:
            logger.error(f"TTS request failed: {e}")
            return b""

    @staticmethod
    def _extract_b64(text: str) -> str:
        needle = '"data":"'
        pos = text.find(needle)
        if pos < 0:
            return ""
        start = pos + len(needle)
        end = text.find('"', start)
        if end < 0:
            return ""
        return text[start:end]

    @staticmethod
    def _wav_to_pcm(wav_data: bytes) -> bytes:
        """Convert WAV to raw PCM, handling various sample rates."""
        try:
            buf = io.BytesIO(wav_data)
            with wave.open(buf, "rb") as wf:
                return wf.readframes(wf.getnframes())
        except Exception as e:
            logger.error(f"WAV decode failed: {e}")
            return b""
