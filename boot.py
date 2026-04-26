#!/usr/bin/env python3
"""
boot.py — bootctl utility for the STM32 BCP/FWP bootloader.

Usage:
    python boot.py help
    python boot.py flash <path> [--slot <1|2>] [--no-verify] [--json] [--port DEV] [--baud N]
    python boot.py status [--slot <1|2>] [--json] [--port DEV] [--baud N]
    python boot.py run [--slot <1|2>] [--json] [--port DEV] [--baud N]
    python boot.py version [--json] [--port DEV] [--baud N]

Requires: pyserial (`pip install pyserial`).
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import time
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from typing import Callable, Optional

try:
    import serial  # pyserial
except ImportError:
    serial = None  # type: ignore[assignment]


# =============================================================================
# CRC
# =============================================================================


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc & 0xFFFF


def crc32_iso_hdlc(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return (crc ^ 0xFFFFFFFF) & 0xFFFFFFFF


BCP_SOF = 0xAA
BCP_REQUEST_HEADER_SIZE = 2
BCP_RESPONSE_HEADER_SIZE = 3
BCP_MAX_DATA_LENGTH = 255


class BcpCommand(IntEnum):
    FLASH = 0x01
    VERIFY = 0x02
    RUN = 0x03
    VERSION = 0x04


class BcpStatus(IntEnum):
    OK = 0x00
    UNKNOWN_COMMAND = 0x01
    INVALID_DATA = 0x02
    BAD_CRC = 0x03
    INVALID_SLOT = 0x04
    INTERNAL_ERROR = 0x05


def bcp_status_name(value: int) -> str:
    try:
        return BcpStatus(value).name
    except ValueError:
        return f"UNKNOWN(0x{value:02X})"


@dataclass
class BcpResponse:
    command: int
    status: int
    data: bytes

    @property
    def is_ok(self) -> bool:
        return self.status == BcpStatus.OK

    @property
    def status_name(self) -> str:
        return bcp_status_name(self.status)


def build_bcp_request(command: int, data: bytes = b"") -> bytes:
    """Encode a BCP request frame ready to write to the wire."""
    if len(data) > BCP_MAX_DATA_LENGTH:
        raise ValueError(f"BCP data length {len(data)} exceeds {BCP_MAX_DATA_LENGTH}")
    body = bytes([command & 0xFF, len(data)]) + data
    crc = crc16_modbus(body)
    # CRC big-endian on the wire (high byte first)
    return bytes([BCP_SOF]) + body + bytes([(crc >> 8) & 0xFF, crc & 0xFF])


def parse_bcp_response(frame: bytes) -> BcpResponse:
    """Decode a complete BCP response frame; raises BcpError on bad CRC/format."""
    if len(frame) < 1 + BCP_RESPONSE_HEADER_SIZE + 2:
        raise BcpError(f"BCP frame too short: {len(frame)} bytes")
    if frame[0] != BCP_SOF:
        raise BcpError(f"BCP SOF mismatch: got 0x{frame[0]:02X}")
    command, status, length = frame[1], frame[2], frame[3]
    expected_total = 1 + BCP_RESPONSE_HEADER_SIZE + length + 2
    if len(frame) != expected_total:
        raise BcpError(f"BCP frame length {len(frame)} != expected {expected_total}")
    data = frame[4 : 4 + length]
    crc_received = (frame[-2] << 8) | frame[-1]  # big-endian
    crc_expected = crc16_modbus(frame[1 : 1 + BCP_RESPONSE_HEADER_SIZE + length])
    if crc_received != crc_expected:
        raise BcpError(
            f"BCP CRC mismatch: got 0x{crc_received:04X}, expected 0x{crc_expected:04X}"
        )
    return BcpResponse(command=command, status=status, data=data)


class BcpError(Exception):
    """Transport / framing / CRC error in BCP."""


class BcpClient:
    """Send BCP requests and read BCP responses over a pyserial port."""

    def __init__(self, ser):
        self.ser = ser

    def send(self, command: int, data: bytes = b"") -> None:
        self.ser.write(build_bcp_request(command, data))
        self.ser.flush()

    def recv(self, timeout: Optional[float] = None) -> BcpResponse:
        old_timeout = self.ser.timeout
        if timeout is not None:
            self.ser.timeout = timeout
        try:
            # Skip leading garbage until SOF.
            while True:
                b = self._read_exact(1)
                if b[0] == BCP_SOF:
                    break
            header = self._read_exact(BCP_RESPONSE_HEADER_SIZE)
            length = header[2]
            data = self._read_exact(length) if length else b""
            crc_bytes = self._read_exact(2)
            return parse_bcp_response(bytes([BCP_SOF]) + header + data + crc_bytes)
        finally:
            self.ser.timeout = old_timeout

    def request(
        self, command: int, data: bytes = b"", timeout: Optional[float] = None
    ) -> BcpResponse:
        self.send(command, data)
        return self.recv(timeout=timeout)

    def _read_exact(self, n: int) -> bytes:
        buf = bytearray()
        while len(buf) < n:
            chunk = self.ser.read(n - len(buf))
            if not chunk:
                raise BcpError("Timeout reading from serial port")
            buf.extend(chunk)
        return bytes(buf)


FWP_SOF = 0xAA
FWP_TYPE_START = 0x01
FWP_TYPE_DATA = 0x02
FWP_TYPE_END = 0x03
FWP_ACK = 0x06
FWP_NAK = 0x15
FWP_DATA_SIZE = 256
FWP_HEADER_SIZE = 5


class FwpError(Exception):
    pass


def build_fwp_packet(type_: int, seq: int, payload: bytes = b"") -> bytes:
    if len(payload) > FWP_DATA_SIZE:
        raise ValueError(f"FWP payload {len(payload)} exceeds {FWP_DATA_SIZE}")
    header = struct.pack("<BHH", type_, seq, len(payload))  # all little-endian
    body = header + payload
    crc = crc16_modbus(body)
    return bytes([FWP_SOF]) + body + struct.pack("<H", crc)


class FwpClient:
    """Drive a firmware transfer over a pyserial port."""

    def __init__(
        self,
        ser,
        max_retries: int = 5,
        ack_timeout: float = 6.0,
    ):
        self.ser = ser
        self.max_retries = max_retries
        self.ack_timeout = ack_timeout

    def _send_packet(self, type_: int, seq: int, payload: bytes = b"") -> None:
        self.ser.write(build_fwp_packet(type_, seq, payload))
        self.ser.flush()

    def _wait_ack_or_nak(self) -> int:
        old_timeout = self.ser.timeout
        self.ser.timeout = self.ack_timeout
        try:
            b = self.ser.read(1)
            if not b:
                raise FwpError("Timeout waiting for FWP ACK/NAK")
            return b[0]
        finally:
            self.ser.timeout = old_timeout

    def _send_with_retry(self, type_: int, seq: int, payload: bytes = b"") -> None:
        last_response = None
        for attempt in range(1, self.max_retries + 1):
            self._send_packet(type_, seq, payload)
            resp = self._wait_ack_or_nak()
            last_response = resp
            if resp == FWP_ACK:
                return
            if resp == FWP_NAK:
                continue
            raise FwpError(
                f"Unexpected FWP reply 0x{resp:02X} (type=0x{type_:02X}, seq={seq})"
            )
        raise FwpError(
            f"FWP packet (type=0x{type_:02X}, seq={seq}) failed after "
            f"{self.max_retries} retries (last reply: 0x{last_response:02X})"
        )

    def transfer(
        self,
        image: bytes,
        progress_cb: Optional[Callable[[int, int], None]] = None,
    ) -> None:
        total_size = len(image)
        seq = 0

        # START — 4-byte LE total size
        self._send_with_retry(FWP_TYPE_START, seq, struct.pack("<I", total_size))
        seq += 1

        # DATA chunks of up to FWP_DATA_SIZE bytes
        offset = 0
        while offset < total_size:
            chunk = image[offset : offset + FWP_DATA_SIZE]
            self._send_with_retry(FWP_TYPE_DATA, seq, chunk)
            offset += len(chunk)
            seq += 1
            if progress_cb is not None:
                progress_cb(offset, total_size)

        # END — empty payload
        self._send_with_retry(FWP_TYPE_END, seq)


# =============================================================================
# Image metadata sanity checks (image is supplied pre-built by the user)
# =============================================================================

IMAGE_METADATA_SIZE = 32
IMAGE_MAGIC_NUMBER = 0xAAAAAAAA


def validate_image_blob(image: bytes) -> dict:
    """Lightweight host-side sanity check on the image file.

    Strict validation of size/CRC consistency is the BOOTLOADER's job — it
    re-CRCs whatever lands in flash and reports the result via BCP_VERIFY.
    Reproducing that here would just mean two slightly-different validators
    that can disagree (e.g. on whether `size` includes the header, on
    trailing alignment padding, etc.). So we only check the one thing that
    catches the common "user fed in the wrong file" mistake — the magic
    word — and surface the rest of the metadata for transparency.

    Metadata layout (little-endian, packed, 32 bytes total):
        magic     u32  must be 0xAAAAAAAA
        size      u32  body length the bootloader uses for CRC
        crc       u32  expected CRC of the body (algorithm decided by bootloader)
        ver_major u8
        ver_minor u8
        ver_patch u8
        reserved  u8[17]
    """
    if len(image) < IMAGE_METADATA_SIZE:
        raise ValueError(
            f"Image too small ({len(image)} bytes); need at least "
            f"{IMAGE_METADATA_SIZE} bytes for the metadata header"
        )
    magic, size, crc = struct.unpack_from("<III", image, 0)
    major, minor, patch = struct.unpack_from("<BBB", image, 12)

    if magic != IMAGE_MAGIC_NUMBER:
        raise ValueError(
            f"Image magic mismatch: got 0x{magic:08X}, expected 0x{IMAGE_MAGIC_NUMBER:08X}. "
            "Did you forget to prepend the metadata header?"
        )
    return {
        "size": size,
        "crc": crc,
        "version": {
            "major": major,
            "minor": minor,
            "patch": patch,
            "string": f"{major}.{minor}.{patch}",
        },
        "file_size": len(image),
        "body_in_file": len(image) - IMAGE_METADATA_SIZE,
    }


# =============================================================================
# bootctl high-level commands
# =============================================================================


def _parse_verify_response(data: bytes) -> dict:
    """Decode a BCP_VERIFY response payload from the bootloader."""
    if len(data) == 0:
        return {"valid": False, "error": "empty verify response"}
    valid_marker = data[0]
    if valid_marker != 1:
        return {"valid": False}
    if len(data) < 12:
        return {"valid": False, "error": f"short verify response: {len(data)} bytes"}
    major, minor, patch = data[1], data[2], data[3]
    crc = struct.unpack(">I", data[4:8])[0]  # handler writes big-endian
    size = struct.unpack(">I", data[8:12])[0]
    return {
        "valid": True,
        "version": {
            "major": major,
            "minor": minor,
            "patch": patch,
            "string": f"{major}.{minor}.{patch}",
        },
        "crc": crc,
        "crc_hex": f"0x{crc:08X}",
        "size": size,
    }


def _open_serial(port: str, baud: int, timeout: float = 5.0):
    if serial is None:
        raise RuntimeError("pyserial is not installed (pip install pyserial)")
    return serial.Serial(port, baud, timeout=timeout)


def cmd_flash(args) -> dict:
    image = Path(args.path).read_bytes()
    image_info = validate_image_blob(image)

    with _open_serial(args.port, args.baud) as ser:
        bcp = BcpClient(ser)
        fwp = FwpClient(ser)

        # Drain anything stale before starting.
        ser.reset_input_buffer()

        if not args.json:
            print(
                f"Flashing slot {args.slot}: file {image_info['file_size']} bytes "
                f"(header says version {image_info['version']['string']}, "
                f"body size {image_info['size']}, CRC 0x{image_info['crc']:08X})",
                file=sys.stderr,
            )

        # 1) Kick off flashing. Bootloader does NOT reply at this point — it
        #    erases pages and immediately enters fwp_receive(), so the BCP
        #    response for FLASH only arrives after the FWP transfer completes.
        bcp.send(BcpCommand.FLASH, bytes([args.slot]))

        # Small grace period to let the bootloader finish erasing pages
        # before we start hammering the START packet at it.
        time.sleep(0.05)

        # 2) Stream the image over FWP.
        progress_cb = None if args.json else _make_progress_cb()
        fwp.transfer(image, progress_cb=progress_cb)

        # 3) Read the BCP response that handle_flash queued after fwp_receive.
        flash_resp = bcp.recv(timeout=10.0)
        if flash_resp.command != BcpCommand.FLASH:
            return {
                "ok": False,
                "command": "flash",
                "slot": args.slot,
                "error": f"unexpected command in flash response: 0x{flash_resp.command:02X}",
            }
        if not flash_resp.is_ok:
            return {
                "ok": False,
                "command": "flash",
                "slot": args.slot,
                "error": flash_resp.status_name,
            }

        # 4) Optional verify after flashing.
        verify_info = None
        if not args.no_verify:
            v = bcp.request(BcpCommand.VERIFY, bytes([args.slot]), timeout=10.0)
            if not v.is_ok:
                return {
                    "ok": False,
                    "command": "flash",
                    "slot": args.slot,
                    "error": f"verify failed: {v.status_name}",
                }
            verify_info = _parse_verify_response(v.data)
            if not verify_info.get("valid"):
                return {
                    "ok": False,
                    "command": "flash",
                    "slot": args.slot,
                    "error": "post-flash verify reports image as invalid",
                    "verify": verify_info,
                }

    result = {"ok": True, "command": "flash", "slot": args.slot, "bytes": len(image)}
    if verify_info is not None:
        result["verify"] = verify_info
    return result


def cmd_status(args) -> dict:
    slots = [args.slot] if args.slot is not None else [1, 2]
    slot_info: dict = {}
    with _open_serial(args.port, args.baud) as ser:
        bcp = BcpClient(ser)
        ser.reset_input_buffer()
        for slot in slots:
            resp = bcp.request(BcpCommand.VERIFY, bytes([slot]), timeout=10.0)
            if not resp.is_ok:
                slot_info[slot] = {"error": resp.status_name}
            else:
                slot_info[slot] = _parse_verify_response(resp.data)
    return {"ok": True, "command": "status", "slots": slot_info}


def cmd_run(args) -> dict:
    with _open_serial(args.port, args.baud) as ser:
        bcp = BcpClient(ser)
        ser.reset_input_buffer()
        resp = bcp.request(BcpCommand.RUN, bytes([args.slot]), timeout=5.0)
        if not resp.is_ok:
            return {
                "ok": False,
                "command": "run",
                "slot": args.slot,
                "error": resp.status_name,
            }
    return {"ok": True, "command": "run", "slot": args.slot}


def cmd_version(args) -> dict:
    with _open_serial(args.port, args.baud) as ser:
        bcp = BcpClient(ser)
        ser.reset_input_buffer()
        resp = bcp.request(BcpCommand.VERSION, b"", timeout=5.0)
        if not resp.is_ok:
            return {"ok": False, "command": "version", "error": resp.status_name}
        if len(resp.data) < 3:
            return {
                "ok": False,
                "command": "version",
                "error": f"short version response: {len(resp.data)} bytes",
            }
        major, minor, patch = resp.data[0], resp.data[1], resp.data[2]
    return {
        "ok": True,
        "command": "version",
        "bootloader": {
            "major": major,
            "minor": minor,
            "patch": patch,
            "string": f"{major}.{minor}.{patch}",
        },
    }


def _make_progress_cb() -> Callable[[int, int], None]:
    last_percent = [-1]

    def cb(done: int, total: int) -> None:
        percent = int(done * 100 / total) if total else 100
        if percent != last_percent[0]:
            last_percent[0] = percent
            end = "\n" if done == total else ""
            print(
                f"\r  {done}/{total} bytes ({percent}%)",
                end=end,
                file=sys.stderr,
                flush=True,
            )

    return cb


HELP_TEXT = """\
Syntax:
bootctl command [flags]
Commands:
 - flash <path> [--slot <1|2>] [--no-verify] [--json]
 - status [--slot <1|2>] [--json]
 - run [--slot <1|2>] [--json]
 - version [--json]
Common transport flags:
 - --port <device>   Serial port (default: /dev/ttyACM0)
 - --baud <rate>     Baud rate (default: 115200)
"""


def format_human(result: dict) -> str:
    cmd = result.get("command", "")

    if not result.get("ok", False):
        prefix = f"{cmd}: " if cmd else ""
        return f"ERROR: {prefix}{result.get('error', 'unknown error')}"

    if cmd == "flash":
        out = [f"Flash to slot {result['slot']}: OK ({result['bytes']} bytes)"]
        v = result.get("verify")
        if v is not None and v.get("valid"):
            out.append(
                f"  Verified: version {v['version']['string']}, "
                f"size {v['size']} bytes, CRC {v['crc_hex']}"
            )
        return "\n".join(out)

    if cmd == "run":
        return f"Run slot {result['slot']}: OK"

    if cmd == "version":
        b = result["bootloader"]
        return f"Bootloader version: {b['string']}"

    if cmd == "status":
        lines = []
        for slot, info in result["slots"].items():
            lines.append(f"Slot {slot}:")
            if "error" in info:
                lines.append(f"  ERROR: {info['error']}")
            elif not info.get("valid"):
                lines.append("  empty / invalid image")
            else:
                lines.append(f"  version : {info['version']['string']}")
                lines.append(f"  size    : {info['size']} bytes")
                lines.append(f"  CRC32   : {info['crc_hex']}")
        return "\n".join(lines)

    return json.dumps(result, indent=2)


def _add_transport_flags(p: argparse.ArgumentParser) -> None:
    p.add_argument(
        "--port",
        default="/dev/ttyACM0",
        help="Serial port device (default: /dev/ttyACM0)",
    )
    p.add_argument(
        "--baud", type=int, default=115200, help="Baud rate (default: 115200)"
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="bootctl",
        description="Control the STM32 BCP/FWP bootloader.",
        add_help=False,
    )

    sub = parser.add_subparsers(dest="command")

    sub.add_parser("help", help="Show usage information")

    p_flash = sub.add_parser("flash", help="Flash a firmware image into a slot")
    p_flash.add_argument(
        "path", help="Path to a pre-built image (with metadata header)"
    )
    p_flash.add_argument("--slot", type=int, choices=[1, 2], default=1)
    p_flash.add_argument(
        "--no-verify", action="store_true", help="Skip post-flash verification"
    )
    p_flash.add_argument("--json", action="store_true")
    _add_transport_flags(p_flash)

    p_status = sub.add_parser("status", help="Show information about flashed images")
    p_status.add_argument("--slot", type=int, choices=[1, 2], default=None)
    p_status.add_argument("--json", action="store_true")
    _add_transport_flags(p_status)

    p_run = sub.add_parser("run", help="Boot into a flashed image")
    p_run.add_argument("--slot", type=int, choices=[1, 2], default=1)
    p_run.add_argument("--json", action="store_true")
    _add_transport_flags(p_run)

    p_version = sub.add_parser("version", help="Print bootloader version")
    p_version.add_argument("--json", action="store_true")
    _add_transport_flags(p_version)

    return parser


def main(argv=None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.command in (None, "help"):
        # The help command never opens the serial port and never produces JSON;
        # match the spec exactly.
        sys.stdout.write(HELP_TEXT)
        return 0

    handlers = {
        "flash": cmd_flash,
        "status": cmd_status,
        "run": cmd_run,
        "version": cmd_version,
    }
    handler = handlers[args.command]

    try:
        result = handler(args)
    except FileNotFoundError as e:
        result = {
            "ok": False,
            "command": args.command,
            "error": f"file not found: {e.filename}",
        }
    except (BcpError, FwpError, ValueError) as e:
        result = {"ok": False, "command": args.command, "error": str(e)}
    except Exception as e:
        # Catch-all so we can still emit JSON instead of a Python traceback.
        if serial is not None and isinstance(e, serial.SerialException):
            result = {
                "ok": False,
                "command": args.command,
                "error": f"serial port error: {e}",
            }
        else:
            raise

    if getattr(args, "json", False):
        print(json.dumps(result, indent=2))
    else:
        print(format_human(result))

    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
