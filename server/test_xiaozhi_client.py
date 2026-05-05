#!/usr/bin/env python3
"""Xiaozhi Protocol Test Client — PC microphone to server, TTS playback.

Requirements: pip install pyaudio pyogg websockets

Usage:
    python test_xiaozhi_client.py [--server ws://59.110.161.101:7070]
"""

import argparse
import asyncio
import ctypes
import json
import struct
import sys
import pyaudio
import websockets
from pyogg.opus import (
    opus_encoder_create, opus_decoder_create,
    opus_encode, opus_decode,
    opus_encoder_destroy, opus_decoder_destroy,
    opus_encoder_ctl, opus_decoder_ctl,
    opus_strerror,
    OPUS_APPLICATION_VOIP, OPUS_OK,
    OPUS_SET_BITRATE_REQUEST,
    c_int, c_int16,
)

# ── Audio config ────────────────────────────────────────────────────
SAMPLE_RATE = 16000
FRAME_MS = 60
SAMPLES = 960
CHANNELS = 1

# ── Binary protocol v3 ──────────────────────────────────────────────
BIN_TYPE_OPUS = 0
HEADER_SIZE = 4

def pack_binary_frame(ftype: int, payload: bytes) -> bytes:
    return struct.pack(">BBH", ftype, 0, len(payload)) + payload

def unpack_binary_frame(data: bytes):
    if len(data) < HEADER_SIZE: return None
    pl = (data[2] << 8) | data[3]
    if len(data) < HEADER_SIZE + pl: return None
    return data[0], data[HEADER_SIZE:HEADER_SIZE + pl]


# ── Opus codec (pyogg low-level) ────────────────────────────────────
class OpusCodec:
    def __init__(self):
        err = ctypes.c_int()
        self._enc = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP, ctypes.byref(err))
        if err.value != OPUS_OK:
            raise RuntimeError(f"Encoder: {opus_strerror(err.value)}")
        # Set 32kbps
        bitrate = ctypes.c_int(32000)
        opus_encoder_ctl(self._enc, OPUS_SET_BITRATE_REQUEST, bitrate)

        self._dec = opus_decoder_create(SAMPLE_RATE, CHANNELS, ctypes.byref(err))
        if err.value != OPUS_OK:
            raise RuntimeError(f"Decoder: {opus_strerror(err.value)}")

    def encode(self, pcm: bytes) -> bytes:
        # Convert bytes → ctypes short array
        pad = SAMPLES * 2 - len(pcm)
        if pad > 0:
            pcm = pcm + b"\x00" * pad
        arr = (c_int16 * SAMPLES).from_buffer_copy(pcm)
        out = (ctypes.c_ubyte * 256)()
        n = opus_encode(self._enc, arr, SAMPLES, out, 256)
        return bytes(out[:n]) if n > 0 else b""

    def decode(self, opus_data: bytes) -> bytes:
        out = (c_int16 * (SAMPLES * 2))()
        inbuf = (ctypes.c_ubyte * len(opus_data)).from_buffer_copy(opus_data)
        n = opus_decode(self._dec, inbuf, len(opus_data), out, SAMPLES * 2, 0)
        if n <= 0:
            return b"\x00" * (SAMPLES * 2)
        return ctypes.string_at(ctypes.addressof(out), n * 2)

    def __del__(self):
        if hasattr(self, "_enc") and self._enc:
            opus_encoder_destroy(self._enc)
        if hasattr(self, "_dec") and self._dec:
            opus_decoder_destroy(self._dec)


# ── Audio I/O ────────────────────────────────────────────────────────
class AudioIO:
    def __init__(self):
        self.pa = pyaudio.PyAudio()
        self._queue = asyncio.Queue()
        self._stream = None
        self._spk = None

    def start_mic(self):
        def cb(in_data, fc, ti, st):
            self._queue.put_nowait(in_data)
            return (None, pyaudio.paContinue)
        self._stream = self.pa.open(
            format=pyaudio.paInt16, channels=CHANNELS,
            rate=SAMPLE_RATE, input=True,
            frames_per_buffer=SAMPLES, stream_callback=cb,
        )

    def stop_mic(self):
        if self._stream:
            self._stream.stop_stream(); self._stream.close(); self._stream = None

    async def read(self) -> bytes | None:
        try:
            return await asyncio.wait_for(self._queue.get(), timeout=0.1)
        except asyncio.TimeoutError:
            return None

    def play(self, pcm: bytes):
        if not self._spk:
            self._spk = self.pa.open(
                format=pyaudio.paInt16, channels=CHANNELS,
                rate=SAMPLE_RATE, output=True,
                frames_per_buffer=SAMPLES,
            )
        self._spk.write(pcm)

    def close(self):
        self.stop_mic()
        if self._spk:
            self._spk.stop_stream(); self._spk.close()
        self.pa.terminate()


# ── Client ───────────────────────────────────────────────────────────
async def run(server: str):
    print(f"→ {server}")
    codec = OpusCodec()
    audio = AudioIO()

    listening = True
    speaking = False

    async with websockets.connect(server, max_size=2 ** 20) as ws:
        # hello
        await ws.send(json.dumps({
            "type": "hello", "version": 2, "transport": "websocket",
            "features": {"aec": False, "mcp": False},
            "audio_params": {"format": "opus", "sample_rate": SAMPLE_RATE,
                             "channels": CHANNELS, "frame_duration": FRAME_MS},
        }))

        # listen
        await ws.send(json.dumps({"type": "listen", "state": "start", "mode": "manual"}))

        audio.start_mic()
        print("🎤 请说话... (Ctrl+C 退出)\n")

        async def sender():
            nonlocal listening, speaking
            while True:
                pcm = await audio.read()
                if pcm and listening and not speaking:
                    opus = codec.encode(pcm)
                    await ws.send(pack_binary_frame(BIN_TYPE_OPUS, opus))
                    await asyncio.sleep(0.055)
                else:
                    await asyncio.sleep(0.01)

        t = asyncio.create_task(sender())
        try:
            async for msg in ws:
                if isinstance(msg, bytes):
                    r = unpack_binary_frame(msg)
                    if r and r[0] == BIN_TYPE_OPUS:
                        audio.play(codec.decode(r[1]))
                else:
                    m = json.loads(msg)
                    tp = m.get("type", "")
                    if tp == "stt":
                        print(f"\n📝 {m.get('text','')}")
                    elif tp == "tts":
                        s = m.get("state", "")
                        if s == "start":
                            speaking = True; listening = False
                            sys.stdout.write(f"🔊 {m.get('text','')}\n"); sys.stdout.flush()
                        elif s == "stop":
                            speaking = False; listening = True
                            print("✅ 继续...\n")
        except KeyboardInterrupt:
            print("\n👋")
        finally:
            t.cancel(); audio.close()


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--server", default="ws://59.110.161.101:7070")
    a = p.parse_args()
    try:
        asyncio.run(run(a.server))
    except KeyboardInterrupt:
        pass
