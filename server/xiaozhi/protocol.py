"""Xiaozhi binary protocol v3 — frame encode/decode."""

import struct
import json
from dataclasses import dataclass

# Frame types
BIN_TYPE_OPUS = 0
BIN_TYPE_JSON = 1

# Opus audio params (matches device)
OPUS_SAMPLE_RATE = 16000
OPUS_FRAME_MS = 60
OPUS_SAMPLES = 960  # 16000 * 60 / 1000
OPUS_CHANNELS = 1

HEADER_SIZE = 4  # type(1) + reserved(1) + payload_size(2)


def pack_binary_frame(frame_type: int, payload: bytes) -> bytes:
    """Pack a binary frame for sending to device."""
    size = len(payload)
    header = struct.pack(">BBH", frame_type, 0, size)
    return header + payload


def unpack_binary_frame(data: bytes):
    """Unpack a binary frame received from device.
    Returns (frame_type, payload) or None if invalid.
    """
    if len(data) < HEADER_SIZE:
        return None
    frame_type = data[0]
    payload_size = (data[2] << 8) | data[3]
    if len(data) < HEADER_SIZE + payload_size:
        return None
    return frame_type, data[HEADER_SIZE:HEADER_SIZE + payload_size]


def pack_opus_frame(opus_data: bytes) -> bytes:
    """Pack an Opus audio frame."""
    return pack_binary_frame(BIN_TYPE_OPUS, opus_data)


def pack_json_frame(obj: dict) -> bytes:
    """Pack a JSON message frame."""
    text = json.dumps(obj, ensure_ascii=False)
    return pack_binary_frame(BIN_TYPE_JSON, text.encode("utf-8"))


# JSON message builders
def make_hello_response():
    return {
        "type": "hello",
        "transport": "websocket",
        "audio_params": {
            "format": "opus",
            "sample_rate": OPUS_SAMPLE_RATE,
            "channels": OPUS_CHANNELS,
            "frame_duration": OPUS_FRAME_MS,
        },
    }


def make_stt(text: str):
    return {"type": "stt", "text": text}


def make_tts_event(state: str, text: str = ""):
    return {"type": "tts", "state": state, "text": text}
