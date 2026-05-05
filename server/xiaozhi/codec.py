"""Opus encoder/decoder for 16kHz mono 60ms frames."""

import opuslib
from typing import List
from .protocol import OPUS_SAMPLE_RATE, OPUS_CHANNELS, OPUS_SAMPLES, OPUS_FRAME_MS

FRAME_BYTES = OPUS_SAMPLES * 2  # 960 samples * 16-bit = 1920 bytes


class OpusDecoder:
    def __init__(self):
        self._dec = opuslib.Decoder(OPUS_SAMPLE_RATE, OPUS_CHANNELS)

    def decode(self, opus_data: bytes) -> bytes:
        """Decode an Opus frame to PCM bytes (16-bit little-endian).
        Returns empty bytes on decode failure (e.g. FEC/frame loss).
        """
        try:
            return self._dec.decode(opus_data, OPUS_SAMPLES)
        except opuslib.OpusError:
            return b""

    def decode_to_samples(self, opus_data: bytes) -> List[int]:
        """Decode to list of 16-bit samples."""
        pcm = self.decode(opus_data)
        if not pcm:
            return []
        return [
            int.from_bytes(pcm[i:i + 2], "little", signed=True)
            for i in range(0, len(pcm), 2)
        ]


class OpusEncoder:
    def __init__(self):
        self._enc = opuslib.Encoder(
            fs=OPUS_SAMPLE_RATE,
            channels=OPUS_CHANNELS,
            application=opuslib.APPLICATION_VOIP,
        )
        self._enc.bitrate = 32000  # 32kbps — good balance for voice TTS

    def encode(self, pcm_bytes: bytes) -> bytes:
        """Encode PCM bytes (16-bit LE, 960 samples = 1920 bytes) to Opus frame."""
        if len(pcm_bytes) < FRAME_BYTES:
            # Pad short frames with silence
            pcm_bytes = pcm_bytes + b"\x00" * (FRAME_BYTES - len(pcm_bytes))
        return self._enc.encode(pcm_bytes, OPUS_SAMPLES)

    def encode_pcm(self, pcm_bytes: bytes) -> List[bytes]:
        """Encode arbitrary-length PCM into 60ms Opus frames.
        Pads the last frame with silence if needed.
        """
        frames = []
        for i in range(0, len(pcm_bytes), FRAME_BYTES):
            chunk = pcm_bytes[i:i + FRAME_BYTES]
            frames.append(self.encode(chunk))
        return frames
