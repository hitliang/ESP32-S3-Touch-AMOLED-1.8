"""Speech-to-text via Aliyun NLS (WebSocket protocol).

Uses the Aliyun NLS SpeechTranscriber interface over WebSocket
for real-time streaming ASR with one-sentence detection.
"""

import json
import time
import uuid
import asyncio
import logging
import websockets

logger = logging.getLogger("xz.stt")

# Aliyun NLS endpoints
NLS_WS_URL = "wss://nls-gateway-cn-shanghai.aliyuncs.com/ws/v1"


def _mid() -> str:
    """Generate a message_id without dashes (Aliyun NLS requirement)."""
    return str(uuid.uuid4()).replace("-", "")


class AliyunSTT:
    """Aliyun NLS streaming STT over WebSocket."""

    def __init__(self, access_key_id: str, access_key_secret: str, appkey: str):
        self._ak_id = access_key_id
        self._ak_secret = access_key_secret
        self._appkey = appkey
        self._token: str | None = None
        self._token_expiry: float = 0

    async def _get_token(self) -> str:
        """Get or refresh Aliyun NLS token."""
        if self._token and time.time() < self._token_expiry - 300:
            return self._token

        from aliyunsdkcore.client import AcsClient
        from aliyunsdkcore.request import CommonRequest

        client = AcsClient(self._ak_id, self._ak_secret, "cn-shanghai")
        request = CommonRequest()
        request.set_domain("nls-meta.cn-shanghai.aliyuncs.com")
        request.set_version("2019-02-28")
        request.set_action_name("CreateToken")

        loop = asyncio.get_running_loop()
        resp = await loop.run_in_executor(None, client.do_action_with_exception, request)
        data = json.loads(resp)
        token_info = data.get("Token", {})
        self._token = token_info.get("Id", "")
        expire_time = token_info.get("ExpireTime", 0)
        self._token_expiry = expire_time if isinstance(expire_time, (int, float)) else time.time() + 3600

        logger.info(f"NLS token refreshed, expires {self._token_expiry}")
        return self._token

    async def transcribe(self, pcm: bytes, sample_rate: int = 16000) -> str:
        """Transcribe PCM audio via Aliyun NLS WebSocket."""
        token = await self._get_token()
        ws_url = f"{NLS_WS_URL}?token={token}"

        task_id = _mid()
        result_text = ""
        error_msg = ""
        done = asyncio.Event()

        try:
            async with websockets.connect(ws_url, max_size=2 ** 20) as ws:
                # Start
                start_msg = {
                    "header": {
                        "namespace": "SpeechTranscriber",
                        "name": "StartTranscription",
                        "message_id": _mid(),
                        "task_id": task_id,
                        "appkey": self._appkey,
                    },
                    "payload": {
                        "format": "pcm",
                        "sample_rate": sample_rate,
                        "enable_intermediate_result": False,
                        "enable_punctuation_prediction": True,
                        "enable_inverse_text_normalization": True,
                    },
                }
                await ws.send(json.dumps(start_msg, ensure_ascii=False))

                # Wait for start ack
                try:
                    ack = await asyncio.wait_for(ws.recv(), timeout=5)
                    ack_data = json.loads(ack)
                    ack_name = ack_data.get("header", {}).get("name", "")
                    if ack_name == "TaskFailed":
                        error_msg = ack_data.get("header", {}).get("status_text", "task failed")
                        logger.error(f"NLS start failed: {error_msg}")
                        return ""
                    logger.info(f"NLS ack: {ack_name}")
                except asyncio.TimeoutError:
                    logger.error("NLS start timeout")
                    return ""

                # Stream audio in 40ms chunks
                chunk_size = 1280  # 40ms of 16kHz mono 16bit
                for i in range(0, len(pcm), chunk_size):
                    await ws.send(pcm[i:i + chunk_size])

                await asyncio.sleep(0.2)

                # Stop
                stop_msg = {
                    "header": {
                        "namespace": "SpeechTranscriber",
                        "name": "StopTranscription",
                        "message_id": _mid(),
                        "task_id": task_id,
                        "appkey": self._appkey,
                    },
                }
                await ws.send(json.dumps(stop_msg, ensure_ascii=False))

                # Collect results
                async for raw_msg in ws:
                    msg = json.loads(raw_msg)
                    header = msg.get("header", {})
                    name = header.get("name", "")
                    payload = msg.get("payload", {})

                    if name == "TranscriptionResultChanged":
                        text = payload.get("result", "")
                        if text:
                            result_text = text
                            logger.info(f"NLS intermediate: {text}")

                    elif name == "SentenceEnd":
                        text = payload.get("result", "")
                        if text:
                            result_text = text
                            logger.info(f"NLS sentence end: {text}")

                    elif name == "TranscriptionCompleted":
                        logger.info("NLS completed")
                        done.set()
                        break

                    elif name == "TaskFailed":
                        error_msg = header.get("status_text", str(header.get("status", "")))
                        logger.error(f"NLS task failed: {error_msg}")
                        done.set()
                        break

                try:
                    await asyncio.wait_for(done.wait(), timeout=30.0)
                except asyncio.TimeoutError:
                    logger.error("NLS timed out waiting for result")

        except websockets.exceptions.ConnectionClosed as e:
            logger.error(f"NLS connection closed: code={e.code} reason={e.reason}")

        except Exception as e:
            logger.error(f"NLS error: {e}")

        text = result_text.strip()
        if text:
            logger.info(f"STT: {text}")
        else:
            logger.warning(f"STT: no result (error={error_msg or 'timeout/empty'})")

        return text

    async def close(self):
        pass
