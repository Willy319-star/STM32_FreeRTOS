#!/usr/bin/env python3
"""
Parse STM32 sensor binary protocol frames from UART bytes.

Examples:
  python scripts/uart_protocol_parser.py --port COM6 --baud 115200
  python scripts/uart_protocol_parser.py --hex "AA 55 01 04 01 00 04 00 ..."
  python scripts/uart_protocol_parser.py --file uart_dump.bin
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
from dataclasses import dataclass


SOF = b"\xAA\x55"
VERSION = 0x01
HEADER_SIZE = 12
CRC_SIZE = 2
MAX_PAYLOAD = 64

MSG_DHT11 = 0x01
MSG_MPU6050 = 0x02
MSG_VL53L0X = 0x03
MSG_POT = 0x04
MSG_HEARTBEAT = 0x10


@dataclass
class Frame:
    msg_type: int
    seq: int
    tick: int
    payload: bytes


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def parse_frame(frame: bytes) -> Frame | None:
    if len(frame) < HEADER_SIZE + CRC_SIZE:
        return None
    if frame[0:2] != SOF:
        return None
    if frame[2] != VERSION:
        return None

    msg_type = frame[3]
    seq = struct.unpack_from("<H", frame, 4)[0]
    payload_len = struct.unpack_from("<H", frame, 6)[0]
    tick = struct.unpack_from("<I", frame, 8)[0]
    expected_len = HEADER_SIZE + payload_len + CRC_SIZE
    if payload_len > MAX_PAYLOAD or len(frame) != expected_len:
        return None

    got_crc = struct.unpack_from("<H", frame, HEADER_SIZE + payload_len)[0]
    calc_crc = crc16_ccitt_false(frame[: HEADER_SIZE + payload_len])
    if got_crc != calc_crc:
        return None

    return Frame(
        msg_type=msg_type,
        seq=seq,
        tick=tick,
        payload=frame[HEADER_SIZE : HEADER_SIZE + payload_len],
    )


def decode_payload(frame: Frame) -> str:
    payload = frame.payload

    if frame.msg_type == MSG_DHT11 and len(payload) == 3:
        valid, temp, hum = payload
        status = "OK" if valid else "FAIL"
        return f"[DHT11 {status}] seq={frame.seq} tick={frame.tick} temp={temp}C hum={hum}%"

    if frame.msg_type == MSG_MPU6050 and len(payload) == 15:
        valid = payload[0]
        addr = payload[1]
        whoami = payload[2]
        ax, ay, az, gx, gy, gz = struct.unpack_from("<hhhhhh", payload, 3)
        status = "OK" if valid else "FAIL"
        return (
            f"[MPU6050 {status}] seq={frame.seq} tick={frame.tick} "
            f"addr=0x{addr:02X} id=0x{whoami:02X} "
            f"acc={ax},{ay},{az} gyro={gx},{gy},{gz}"
        )

    if frame.msg_type == MSG_VL53L0X and len(payload) == 6:
        valid = payload[0]
        addr = payload[1]
        model_id = payload[2]
        range_valid = payload[3]
        distance_mm = struct.unpack_from("<H", payload, 4)[0]
        if valid and range_valid:
            status = "RANGE_OK"
        elif valid:
            status = "ONLINE"
        else:
            status = "FAIL"
        return (
            f"[VL53L0X {status}] seq={frame.seq} tick={frame.tick} "
            f"addr=0x{addr:02X} model=0x{model_id:02X} dist={distance_mm}mm"
        )

    if frame.msg_type == MSG_POT and len(payload) == 4:
        valid = payload[0]
        raw = struct.unpack_from("<H", payload, 1)[0]
        percent = payload[3]
        status = "OK" if valid else "FAIL"
        return f"[POT {status}] seq={frame.seq} tick={frame.tick} raw={raw} percent={percent}%"

    return (
        f"[UNKNOWN] seq={frame.seq} tick={frame.tick} "
        f"type=0x{frame.msg_type:02X} payload={payload.hex(' ').upper()}"
    )


class StreamParser:
    def __init__(self, show_text: bool = False) -> None:
        self.buf = bytearray()
        self.show_text = show_text

    def feed(self, data: bytes) -> list[str]:
        self.buf.extend(data)
        lines: list[str] = []

        while True:
            sof_index = self.buf.find(SOF)
            if sof_index < 0:
                self._emit_text(lines, len(self.buf))
                del self.buf[:]
                return lines

            if sof_index > 0:
                self._emit_text(lines, sof_index)
                del self.buf[:sof_index]

            if len(self.buf) < HEADER_SIZE:
                return lines

            payload_len = struct.unpack_from("<H", self.buf, 6)[0]
            if payload_len > MAX_PAYLOAD:
                del self.buf[0]
                continue

            frame_len = HEADER_SIZE + payload_len + CRC_SIZE
            if len(self.buf) < frame_len:
                return lines

            raw_frame = bytes(self.buf[:frame_len])
            frame = parse_frame(raw_frame)
            if frame is None:
                lines.append(f"[CRC_OR_FORMAT_ERROR] raw={raw_frame.hex(' ').upper()}")
                del self.buf[0]
                continue
            del self.buf[:frame_len]
            lines.append(decode_payload(frame))

    def _emit_text(self, lines: list[str], size: int) -> None:
        if not self.show_text or size <= 0:
            return
        text = bytes(self.buf[:size]).decode("ascii", errors="ignore").strip()
        if text:
            lines.append(f"[TEXT] {text}")


def read_from_serial(port: str, baud: int, show_text: bool) -> int:
    try:
        import serial  # type: ignore
    except ImportError:
        print("pyserial is not installed. Install it with: python -m pip install pyserial", file=sys.stderr)
        return 2

    parser = StreamParser(show_text=show_text)
    with serial.Serial(port, baudrate=baud, timeout=0.2) as ser:
        print(f"Listening on {port} @ {baud}. Press Ctrl+C to stop.")
        while True:
            data = ser.read(256)
            if not data:
                time.sleep(0.01)
                continue
            for line in parser.feed(data):
                print(line)


def parse_offline(data: bytes, show_text: bool) -> int:
    parser = StreamParser(show_text=show_text)
    for line in parser.feed(data):
        print(line)
    return 0


def main() -> int:
    argp = argparse.ArgumentParser(description="Parse STM32 UART binary protocol frames.")
    source = argp.add_mutually_exclusive_group(required=True)
    source.add_argument("--port", help="Serial port, for example COM6")
    source.add_argument("--file", help="Binary dump file")
    source.add_argument("--hex", help="Hex bytes, for example: 'AA 55 01 ...'")
    argp.add_argument("--baud", type=int, default=115200)
    argp.add_argument("--show-text", action="store_true", help="Also print ASCII text between binary frames")
    args = argp.parse_args()

    if args.port:
        return read_from_serial(args.port, args.baud, args.show_text)
    if args.file:
        with open(args.file, "rb") as f:
            return parse_offline(f.read(), args.show_text)
    if args.hex:
        compact = args.hex.replace("0x", "").replace(",", " ").replace("\n", " ")
        data = bytes.fromhex(compact)
        return parse_offline(data, args.show_text)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
