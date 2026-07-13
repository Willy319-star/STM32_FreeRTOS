#!/usr/bin/env python3
"""Generate deterministic binary test cases for a simple framed protocol.

Frame:
  magic(AA 55) | version(1) | type(1) | seq(2, big-endian)
  | length(2, big-endian) | payload(N) | crc16-ccitt(2, big-endian)

Outputs binary files plus manifest.json. No third-party dependencies.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import random
import struct


MAGIC = b"\xAA\x55"


def crc16_ccitt(data: bytes, init: int = 0xFFFF) -> int:
    crc = init
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def frame(msg_type: int, seq: int, payload: bytes, version: int = 1) -> bytes:
    header = struct.pack(">BBHH", version, msg_type, seq & 0xFFFF, len(payload))
    body = MAGIC + header + payload
    return body + struct.pack(">H", crc16_ccitt(body))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", default="tests/protocol-cases")
    parser.add_argument("--seed", type=int, default=2026)
    args = parser.parse_args()

    out = Path(args.output_dir)
    out.mkdir(parents=True, exist_ok=True)
    rng = random.Random(args.seed)

    good1 = frame(0x01, 1, bytes(range(12)))
    good2 = frame(0x10, 2, b"heartbeat")
    max_payload = bytes((i % 256 for i in range(256)))
    good_max = frame(0x02, 3, max_payload)

    bad_crc = bytearray(good1)
    bad_crc[-1] ^= 0xFF

    lost_byte = good1[:8] + good1[9:]
    noise = bytes(rng.randrange(0, 256) for _ in range(17)) + good1
    consecutive = good1 + good2
    duplicate = good1 + good1
    unknown_command = frame(0xEE, 4, b"unknown")
    too_long_header = MAGIC + struct.pack(">BBHH", 1, 1, 5, 0xFFFF)
    partial_a, partial_b = good2[:5], good2[5:]

    cases = {
        "good_basic.bin": (good1, "valid basic frame"),
        "good_heartbeat.bin": (good2, "valid heartbeat"),
        "good_max_256.bin": (good_max, "valid 256-byte payload"),
        "bad_crc.bin": (bytes(bad_crc), "CRC mismatch"),
        "lost_byte.bin": (lost_byte, "one byte removed"),
        "noise_prefix.bin": (noise, "random noise before a valid frame"),
        "consecutive.bin": (consecutive, "two frames without gap"),
        "duplicate_seq.bin": (duplicate, "same sequence twice"),
        "unknown_command.bin": (unknown_command, "unknown message type"),
        "too_long_length.bin": (too_long_header, "declared length exceeds configured maximum"),
        "partial_part1.bin": (partial_a, "first fragment"),
        "partial_part2.bin": (partial_b, "second fragment"),
    }

    manifest = {
        "format": "AA55|version|type|seq_be|length_be|payload|crc16_ccitt_be",
        "seed": args.seed,
        "cases": [],
    }

    for name, (data, purpose) in cases.items():
        (out / name).write_bytes(data)
        manifest["cases"].append({"file": name, "bytes": len(data), "purpose": purpose})

    (out / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    print(f"Generated {len(cases)} cases in {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
