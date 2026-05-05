"""Xiaozhi WebSocket server for Reddy voice assistant.

Implements the xiaozhi binary protocol v3 to communicate with ESP32-S3 devices.
Flow: hello handshake → receive Opus audio → VAD → STT → LLM → TTS → send Opus audio
"""

from __future__ import annotations

import asyncio
import json
import logging
import struct
import wave
import io
import time
from collections import deque

import websockets
from websockets.server import WebSocketServerProtocol

from .config import load_config, Config
from .protocol import (
    BIN_TYPE_OPUS, BIN_TYPE_JSON, HEADER_SIZE,
    OPUS_SAMPLE_RATE, OPUS_SAMPLES, OPUS_FRAME_MS,
    OPUS_CHANNELS,
    pack_binary_frame, pack_opus_frame, pack_json_frame,
    make_hello_response, make_stt, make_tts_event,
)
from .codec import OpusDecoder, OpusEncoder
from .stt import AliyunSTT
from .tts import TTSService
from .agent import AgentEngine, LLMClient, ToolExecutor
from .memory import ConversationMemory
from .admin import AdminServer

logger = logging.getLogger("xz.server")

# ── Tool handlers ───────────────────────────────────────────────────
import math as _math

_SAFE_MATH = {
    "sqrt": _math.sqrt, "pow": _math.pow,
    "sin": _math.sin, "cos": _math.cos, "tan": _math.tan,
    "pi": _math.pi, "e": _math.e,
    "floor": _math.floor, "ceil": _math.ceil,
    "log": _math.log, "log10": _math.log10, "log2": _math.log2,
}
_SAFE_BUILTINS = {"abs": abs, "round": round, "min": min, "max": max, "int": int, "float": float}
_SAFE_NAMES = {**_SAFE_BUILTINS, **_SAFE_MATH}


def _safe_eval(expr: str) -> str:
    expr = expr.strip().replace("^", "**").replace("×", "*").replace("÷", "/").replace("（", "(").replace("）", ")")
    allowed = set("0123456789.+-*/%() _abcdefghijklmnopqrstuvwxyz")
    for ch in expr:
        if ch.lower() not in allowed:
            return f"不支持字符: {ch}"
    try:
        compiled = compile(expr, "<calc>", "eval")
        for name in compiled.co_names:
            if name not in _SAFE_NAMES:
                return f"不支持的函数: {name}"
        result = eval(compiled, {"__builtins__": {}}, _SAFE_NAMES)
        if isinstance(result, float):
            if abs(result - round(result)) < 1e-10:
                return str(int(round(result)))
            return f"{result:.6f}".rstrip("0").rstrip(".")
        return str(result)
    except ZeroDivisionError:
        return "不能除以零"
    except Exception as e:
        return f"计算错误: {e}"


async def _calc_handler(expression: str) -> str:
    return f"计算结果: {_safe_eval(expression)}"


# VAD settings
VAD_ENERGY_THRESHOLD = 500       # RMS energy threshold for speech
VAD_SILENCE_FRAMES = 8           # consecutive silent frames to end speech (8*60ms = 480ms)
VAD_MIN_SPEECH_FRAMES = 5        # minimum frames before considering it speech
VAD_MAX_SPEECH_FRAMES = 500      # max frames before auto-cutoff (500*60ms = 30s)

# Audio resampling
TTS_SAMPLE_RATE = 24000  # MiMo outputs 24kHz typically


class DeviceSession:
    """Per-device session state."""

    def __init__(self, ws: WebSocketServerProtocol):
        self.ws = ws
        self.decoder = OpusDecoder()
        self.encoder = OpusEncoder()
        self.audio_buffer = bytearray()
        self.listening = False
        self.connected = False
        self.speaking = False
        self.vad_silence_count = 0
        self.vad_speech_frames = 0
        self.vad_active = False

    def reset_vad(self):
        self.audio_buffer = bytearray()
        self.vad_silence_count = 0
        self.vad_speech_frames = 0
        self.vad_active = False


class XiaozhiServer:
    def __init__(self, config: Config):
        self.config = config
        self.stt = AliyunSTT(
            config.stt.access_key_id,
            config.stt.access_key_secret,
            config.stt.appkey,
        )
        self.tts = TTSService(config.tts)
        self.memory = ConversationMemory(config.memory.db_path)

        self.llm = LLMClient(config.llm)
        self.tools = ToolExecutor()
        self._register_tools()
        self.agent = AgentEngine(
            self.llm, self.memory, self.tools, config.system_prompt,
        )

        self._sessions: dict[str, DeviceSession] = {}

    def _register_tools(self):
        # Calculator tool
        self.tools.register(
            {
                "type": "function",
                "function": {
                    "name": "calculator",
                    "description": "计算数学表达式。支持加减乘除、乘方、括号、以及sqrt/abs/round等函数。例如: '2+3*4', '(15+9)/3', 'sqrt(144)'。Reddy不会口算多位数时可以调用此工具。",
                    "parameters": {
                        "type": "object",
                        "properties": {
                            "expression": {
                                "type": "string",
                                "description": "要计算的数学表达式，如 '3*17' 或 'sqrt(81)'",
                            }
                        },
                        "required": ["expression"],
                    },
                },
            },
            _calc_handler,
        )

    async def start(self):
        await self.memory.load_or_create()

        self._admin = AdminServer(self.config.memory.db_path, self.config.server.host, 7071)
        admin_task = asyncio.create_task(self._admin.start())

        logger.info(f"Server starting on {self.config.server.host}:{self.config.server.port}")
        async with websockets.serve(
            self._handle_connection,
            self.config.server.host,
            self.config.server.port,
            max_size=2 * 1024 * 1024,  # 2MB max message
        ):
            await asyncio.Future()  # run forever

        admin_task.cancel()

    async def _handle_connection(self, ws: WebSocketServerProtocol):
        peer = ws.remote_address
        logger.info(f"Client connected: {peer}")

        session = DeviceSession(ws)
        session_id = f"{peer[0]}:{peer[1]}"
        self._sessions[session_id] = session

        try:
            async for message in ws:
                if isinstance(message, bytes):
                    await self._handle_binary(session, message)
                else:
                    await self._handle_json(session, message)
        except websockets.exceptions.ConnectionClosed:
            logger.info(f"Client disconnected: {peer}")
        except Exception as e:
            logger.error(f"Session error {peer}: {e}")
        finally:
            self._sessions.pop(session_id, None)

    async def _handle_json(self, session: DeviceSession, text: str):
        try:
            msg = json.loads(text)
        except json.JSONDecodeError:
            return

        msg_type = msg.get("type", "")
        logger.info(f"<- JSON: {text[:200]}")

        if msg_type == "hello":
            await self._on_hello(session, msg)
        elif msg_type == "listen":
            await self._on_listen(session, msg)

    async def _handle_binary(self, session: DeviceSession, data: bytes):
        if len(data) < HEADER_SIZE:
            return

        frame_type = data[0]
        payload_size = (data[2] << 8) | data[3]
        payload = data[HEADER_SIZE:HEADER_SIZE + payload_size]

        if frame_type == BIN_TYPE_OPUS:
            await self._on_opus(session, payload)

    async def _on_hello(self, session: DeviceSession, msg: dict):
        # Respond with server hello
        resp = make_hello_response()
        await session.ws.send(json.dumps(resp, ensure_ascii=False))
        session.connected = True
        logger.info("Hello handshake complete")

    async def _on_listen(self, session: DeviceSession, msg: dict):
        state = msg.get("state", "")
        if state == "start":
            session.listening = True
            session.reset_vad()
            logger.info("Listening started")
        elif state == "stop":
            session.listening = False
            logger.info("Listening stopped")

    async def _on_opus(self, session: DeviceSession, opus_data: bytes):
        if not session.listening:
            return
        if session.speaking:
            return  # Ignore audio while TTS is playing

        # Decode Opus → PCM
        pcm = session.decoder.decode(opus_data)
        if not pcm:
            return

        session.audio_buffer.extend(pcm)

        # VAD: check energy
        energy = self._rms(pcm)
        is_speech = energy > VAD_ENERGY_THRESHOLD

        if is_speech:
            session.vad_silence_count = 0
            session.vad_speech_frames += 1

            if not session.vad_active and session.vad_speech_frames >= VAD_MIN_SPEECH_FRAMES:
                session.vad_active = True
        else:
            if session.vad_active:
                session.vad_silence_count += 1

        # Check end of speech
        max_frames = VAD_MAX_SPEECH_FRAMES
        if session.vad_active and (
            session.vad_silence_count >= VAD_SILENCE_FRAMES
            or session.vad_speech_frames >= max_frames
        ):
            await self._process_speech(session)

    async def _process_speech(self, session: DeviceSession):
        """Process accumulated speech audio."""
        pcm = bytes(session.audio_buffer)
        session.reset_vad()
        session.listening = False  # Pause listening during processing

        logger.info(f"Processing speech: {len(pcm)} bytes ({len(pcm)/32000:.1f}s)")

        # STT
        text = await self.stt.transcribe(pcm)
        if not text:
            logger.info("STT returned empty, resuming listening")
            session.listening = True
            return

        # Send STT result to device
        await session.ws.send(json.dumps(make_stt(text), ensure_ascii=False))

        # Agent
        reply = await self.agent.process(text)
        logger.info(f"Agent reply: {reply}")

        if not reply:
            session.listening = True
            return

        # TTS
        await self._send_tts(session, reply)

        # Resume listening
        session.listening = True

    async def _send_tts(self, session: DeviceSession, text: str):
        """Stream TTS to device — send Opus frames as soon as PCM chunks arrive."""
        session.speaking = True

        # Send TTS start event
        await session.ws.send(json.dumps(make_tts_event("start", text), ensure_ascii=False))

        first_frame = True
        total_pcm = 0

        async for pcm_chunk, is_last in self.tts.synthesize_stream(text):
            if pcm_chunk:
                total_pcm += len(pcm_chunk)
                # Resample 24kHz → 16kHz
                pcm_16k = self._resample_24k_to_16k(pcm_chunk)
                # Encode to Opus frames and send immediately
                opus_frames = session.encoder.encode_pcm(pcm_16k)
                for frame in opus_frames:
                    await session.ws.send(pack_opus_frame(frame))
                    if first_frame:
                        logger.info(f"TTS first frame sent: {len(frame)} bytes opus")
                        first_frame = False
                    await asyncio.sleep(0.01)  # minimal pacing for streaming

            if is_last:
                break

        logger.info(f"TTS done: {total_pcm} bytes PCM total")
        await session.ws.send(json.dumps(make_tts_event("stop"), ensure_ascii=False))
        session.speaking = False

    @staticmethod
    def _rms(pcm: bytes) -> float:
        """Calculate RMS energy of PCM audio."""
        if len(pcm) < 2:
            return 0.0
        count = len(pcm) // 2
        total = 0
        for i in range(0, len(pcm), 2):
            sample = int.from_bytes(pcm[i:i + 2], "little", signed=True)
            total += sample * sample
        return (total / count) ** 0.5

    @staticmethod
    def _resample_24k_to_16k(pcm_24k: bytes) -> bytes:
        """Simple resample 24kHz → 16kHz by dropping every 3rd sample."""
        samples_24k = len(pcm_24k) // 2
        out = bytearray()
        for i in range(samples_24k):
            if i % 3 != 2:  # skip every 3rd sample
                out.append(pcm_24k[i * 2])
                out.append(pcm_24k[i * 2 + 1])
        return bytes(out)

    async def close(self):
        await self.stt.close()
        await self.tts.close()
