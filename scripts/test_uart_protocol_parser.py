#!/usr/bin/env python3
"""Unit tests for the STM32 UART protocol stream parser."""

from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import uart_protocol_parser as proto


def build_frame(msg_type: int, seq: int, tick: int, payload: bytes) -> bytes:
    header = bytearray()
    header.extend(proto.SOF)
    header.append(proto.VERSION)
    header.append(msg_type)
    header.extend(struct.pack("<H", seq))
    header.extend(struct.pack("<H", len(payload)))
    header.extend(struct.pack("<I", tick))
    crc = proto.crc16_ccitt_false(bytes(header) + payload)
    return bytes(header) + payload + struct.pack("<H", crc)


class StreamParserTests(unittest.TestCase):
    def test_single_dht11_frame(self) -> None:
        parser = proto.StreamParser()
        frame = build_frame(proto.MSG_DHT11, 7, 1000, bytes([1, 26, 42]))

        self.assertEqual(
            parser.feed(frame),
            ["[DHT11 OK] seq=7 tick=1000 temp=26C hum=42%"],
        )

    def test_split_frame_waits_until_complete(self) -> None:
        parser = proto.StreamParser()
        frame = build_frame(proto.MSG_POT, 8, 2000, bytes([1, 0x34, 0x12, 55]))

        self.assertEqual(parser.feed(frame[:5]), [])
        self.assertEqual(
            parser.feed(frame[5:]),
            ["[POT OK] seq=8 tick=2000 raw=4660 percent=55%"],
        )

    def test_sticky_frames_are_parsed_in_order(self) -> None:
        parser = proto.StreamParser()
        dht = build_frame(proto.MSG_DHT11, 1, 100, bytes([1, 25, 50]))
        pot = build_frame(proto.MSG_POT, 2, 200, bytes([1, 0x00, 0x08, 50]))

        self.assertEqual(
            parser.feed(dht + pot),
            [
                "[DHT11 OK] seq=1 tick=100 temp=25C hum=50%",
                "[POT OK] seq=2 tick=200 raw=2048 percent=50%",
            ],
        )

    def test_text_noise_can_be_shown_or_skipped(self) -> None:
        frame = build_frame(proto.MSG_DHT11, 3, 300, bytes([1, 27, 41]))

        hidden = proto.StreamParser(show_text=False)
        self.assertEqual(
            hidden.feed(b"HEARTBEAT\r\n" + frame),
            ["[DHT11 OK] seq=3 tick=300 temp=27C hum=41%"],
        )

        shown = proto.StreamParser(show_text=True)
        self.assertEqual(
            shown.feed(b"HEARTBEAT\r\n" + frame),
            [
                "[TEXT] HEARTBEAT",
                "[DHT11 OK] seq=3 tick=300 temp=27C hum=41%",
            ],
        )

    def test_bad_crc_recovers_to_next_frame(self) -> None:
        parser = proto.StreamParser()
        bad = bytearray(build_frame(proto.MSG_DHT11, 4, 400, bytes([1, 28, 40])))
        good = build_frame(proto.MSG_DHT11, 5, 500, bytes([1, 29, 39]))
        bad[-1] ^= 0xFF

        lines = parser.feed(bytes(bad) + good)

        self.assertEqual(lines[0].split()[0], "[CRC_OR_FORMAT_ERROR]")
        self.assertEqual(lines[1], "[DHT11 OK] seq=5 tick=500 temp=29C hum=39%")

    def test_mpu6050_payload_decode(self) -> None:
        parser = proto.StreamParser()
        payload = bytes([1, 0x68, 0x74]) + struct.pack("<hhhhhh", -991, -4, -223, -3, 0, 0)
        frame = build_frame(proto.MSG_MPU6050, 9, 900, payload)

        self.assertEqual(
            parser.feed(frame),
            ["[MPU6050 OK] seq=9 tick=900 addr=0x68 id=0x74 acc=-991,-4,-223 gyro=-3,0,0"],
        )


if __name__ == "__main__":
    unittest.main()
