"""Capture the main ESP32 console with DTR/RTS deasserted."""

import argparse
import re
import time
from pathlib import Path

import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--port", default="COM3")
parser.add_argument("--seconds", type=float, default=20)
parser.add_argument("--output", type=Path, default=Path("nomad-boot.log"))
parser.add_argument("--reset", action="store_true", help="Pulse the main ESP32 reset through RTS")
args = parser.parse_args()
port = serial.Serial()
port.port = args.port
port.baudrate = 115200
port.timeout = 0.2
port.dtr = False
port.rts = False
port.open()
if args.reset:
    port.rts = True
    time.sleep(0.1)
    port.rts = False
captured = bytearray()
try:
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        captured.extend(port.read(8192))
finally:
    port.close()
args.output.write_bytes(captured)
print(f"Captured {len(captured)} bytes to {args.output}")
for line in captured.decode("utf-8", errors="replace").splitlines():
    if re.search(r"Nomad|LR11|Bluetooth|BLE |Guru|panic|brownout|restart|exception", line, re.I):
        print(line)
