#!/usr/bin/env python3
"""
Capture the live BLE audio stream from an omi-device-builder board and decode it
to a WAV file.

Protocol (see src/services/transport.c):
  * Audio service  19B10000-E8F2-537E-4F6C-D104768A1214
  * Audio data     19B10001-...  (NOTIFY)  <- we subscribe here
  * Each notification: [id_lo, id_hi, frag_index, <opus bytes...>]
      - id (uint16 LE) increments once per BLE packet (gap => packet loss)
      - frag_index == 0 marks the first fragment of a new Opus frame; >0 are
        continuation fragments of the same frame (only when a frame doesn't fit
        one MTU — usually it does, so one packet == one frame).
  * Codec: Opus, 16 kHz, mono, 20 ms frames (CODEC_ID 21).

Usage:
    pip install bleak opuslib pyogg
    python omi_audio_to_wav.py --seconds 10 --out out.wav
    python omi_audio_to_wav.py --address AA:BB:CC:DD:EE:FF --seconds 15

Notes:
  * Only one central can connect at a time — disconnect the phone/Omi app first.
  * opuslib needs the native libopus. Installing `pyogg` provides a bundled
    opus.dll (Windows) / libopus (mac wheels) that this script auto-loads, so no
    manual setup is usually needed. Otherwise: Linux `apt install libopus0`,
    macOS `brew install opus`.
  * If Opus still can't load, the raw frames are saved to <out>.opus.bin so no
    data is lost — you can decode them later.
"""

import argparse
import asyncio
import struct
import sys
import time
import wave

from bleak import BleakClient, BleakScanner

AUDIO_DATA_UUID = "19b10001-e8f2-537e-4f6c-d104768a1214"

SAMPLE_RATE = 16000
CHANNELS = 1
HEADER_SIZE = 3          # id (2) + fragment index (1)
MAX_DECODE_SAMPLES = 960  # room for up to 60 ms/frame at 16 kHz


class Collector:
    """Reassembles BLE notifications into complete Opus frames."""

    def __init__(self):
        self.frames = []          # list[bytes] of complete Opus packets
        self._cur = bytearray()
        self._started = False     # have we seen the first frag_index == 0 yet?
        self._packets = 0
        self._last_id = None
        self._lost = 0

    def feed(self, data: bytes):
        if len(data) <= HEADER_SIZE:
            return
        self._packets += 1
        pkt_id = data[0] | (data[1] << 8)
        frag = data[2]
        payload = data[HEADER_SIZE:]

        # Packet-loss accounting via the rolling 16-bit id.
        if self._last_id is not None:
            gap = (pkt_id - self._last_id - 1) & 0xFFFF
            if 0 < gap < 1000:
                self._lost += gap
        self._last_id = pkt_id

        if frag == 0:
            if self._started and self._cur:
                self.frames.append(bytes(self._cur))
            self._cur = bytearray(payload)
            self._started = True
        elif self._started:
            self._cur += payload
        # else: joined mid-frame, wait for the next frag==0

    def finish(self):
        if self._started and self._cur:
            self.frames.append(bytes(self._cur))
            self._cur = bytearray()

    def stats(self):
        return self._packets, len(self.frames), self._lost


async def capture(address, name, seconds):
    if address:
        device = await BleakScanner.find_device_by_address(address, timeout=10)
        if not device:
            sys.exit(f"Device {address} not found (is it advertising / not connected elsewhere?)")
    else:
        print(f"Scanning for '{name}' ...")
        device = await BleakScanner.find_device_by_name(name, timeout=15)
        if not device:
            sys.exit(f"'{name}' not found. Make sure it advertises and no phone is connected.")

    coll = Collector()
    loop = asyncio.get_event_loop()
    stop_at = None

    def on_notify(_char, data: bytearray):
        coll.feed(bytes(data))

    print(f"Connecting to {device.address} ...")
    async with BleakClient(device) as client:
        print("Connected. Subscribing to audio and capturing "
              f"{seconds}s (talk near the mic)...")
        await client.start_notify(AUDIO_DATA_UUID, on_notify)
        stop_at = time.monotonic() + seconds
        while time.monotonic() < stop_at:
            await asyncio.sleep(0.2)
        await client.stop_notify(AUDIO_DATA_UUID)

    coll.finish()
    pkts, frames, lost = coll.stats()
    print(f"Captured {pkts} packets, {frames} Opus frames, ~{lost} lost packets.")
    return coll.frames


def _load_opuslib():
    """Import opuslib, making the native libopus discoverable via pyogg's bundled
    copy if it isn't already on the system."""
    import os
    try:
        import pyogg
        d = os.path.dirname(pyogg.__file__)
        if hasattr(os, "add_dll_directory"):
            os.add_dll_directory(d)
        os.environ["PATH"] = d + os.pathsep + os.environ.get("PATH", "")
    except Exception:  # noqa: BLE001
        pass  # fall back to a system-installed libopus
    import opuslib
    return opuslib


def decode_to_wav(frames, out_path):
    try:
        opuslib = _load_opuslib()
    except Exception as e:  # noqa: BLE001
        raw = out_path + ".opus.bin"
        with open(raw, "wb") as f:
            for fr in frames:
                f.write(struct.pack("<H", len(fr)))  # length-prefixed frames
                f.write(fr)
        sys.exit(f"opuslib unavailable ({e}). Raw frames saved to {raw} "
                 "(each: uint16 LE length + Opus bytes). Install libopus to decode.")

    dec = opuslib.Decoder(SAMPLE_RATE, CHANNELS)
    pcm = bytearray()
    bad = 0
    for fr in frames:
        try:
            pcm += dec.decode(fr, MAX_DECODE_SAMPLES)
        except Exception:  # noqa: BLE001
            bad += 1
    with wave.open(out_path, "wb") as w:
        w.setnchannels(CHANNELS)
        w.setsampwidth(2)          # 16-bit
        w.setframerate(SAMPLE_RATE)
        w.writeframes(pcm)
    dur = len(pcm) / 2 / CHANNELS / SAMPLE_RATE
    print(f"Wrote {out_path}: {dur:.1f}s, {len(frames) - bad} frames decoded"
          + (f", {bad} undecodable" if bad else ""))


def main():
    ap = argparse.ArgumentParser(description="Capture omi BLE audio -> WAV")
    ap.add_argument("--name", default="omi-xiao52", help="BLE name to scan for")
    ap.add_argument("--address", help="BLE MAC/UUID (skip scanning by name)")
    ap.add_argument("--seconds", type=float, default=10.0, help="capture duration")
    ap.add_argument("--out", default="omi_audio.wav", help="output WAV path")
    args = ap.parse_args()

    frames = asyncio.run(capture(args.address, args.name, args.seconds))
    if not frames:
        sys.exit("No audio frames received.")
    decode_to_wav(frames, args.out)


if __name__ == "__main__":
    main()
