#!/usr/bin/env python3
"""Provision a sensor-tag-wifi via its serial console.

Waits for the C6's USB CDC to appear, sends provisioning commands over the
console, then exits. Override defaults via CLI flags.
"""
import argparse
import glob
import sys
import time
import serial


PORT_GLOB = "/dev/cu.usbmodem*"
BAUD = 115200
SHELL_PROMPT = b"> "


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--wifi-ssid", default="IOT")
    p.add_argument("--wifi-pass", default="4696930759")
    p.add_argument("--mqtt-host", default="192.168.1.82")
    p.add_argument("--mqtt-port", default="1883")
    p.add_argument("--mqtt-user", default="hivesense")
    p.add_argument("--mqtt-pass", default="hivesense")
    p.add_argument("--tag-name",  default="bench-ds18b20")
    p.add_argument("--sample-int", default="30")
    p.add_argument("--upload-every", default="1")
    p.add_argument("--ota-host",  default="192.168.1.61")
    return p.parse_args()


def build_commands(a: argparse.Namespace) -> list[str]:
    return [
        f"set wifi_ssid {a.wifi_ssid}",
        f"set wifi_pass {a.wifi_pass}",
        f"set mqtt_host {a.mqtt_host}",
        f"set mqtt_port {a.mqtt_port}",
        f"set mqtt_user {a.mqtt_user}",
        f"set mqtt_pass {a.mqtt_pass}",
        f"set tag_name {a.tag_name}",
        f"set sample_int {a.sample_int}",
        f"set upload_every {a.upload_every}",
        f"set ota_host {a.ota_host}",
        "list",
        "exit",
    ]


def find_port(timeout_s: float = 60.0) -> str:
    print(f"[prov] waiting up to {timeout_s:.0f}s for {PORT_GLOB} ...")
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        matches = glob.glob(PORT_GLOB)
        if matches:
            port = matches[0]
            print(f"[prov] found {port}")
            return port
        time.sleep(0.1)
    print("[prov] port never appeared — reconnect the C6 USB cable")
    sys.exit(1)


def open_serial(port: str, timeout_s: float = 5.0) -> serial.Serial:
    deadline = time.time() + timeout_s
    last_err = None
    while time.time() < deadline:
        try:
            return serial.Serial(port, BAUD, timeout=0.05)
        except (serial.SerialException, OSError) as err:
            last_err = err
            time.sleep(0.05)
    raise RuntimeError(f"could not open {port}: {last_err}")


def _read(ser: serial.Serial, n: int) -> bytes:
    try:
        return ser.read(n)
    except serial.SerialException:
        time.sleep(0.05)
        return b""


def wait_for(ser: serial.Serial, needle: bytes, timeout_s: float) -> bool:
    buf = bytearray()
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        chunk = _read(ser, 256)
        if chunk:
            buf.extend(chunk)
            sys.stdout.write(chunk.decode(errors="replace"))
            sys.stdout.flush()
            if needle in buf:
                return True
    return False


def drain(ser: serial.Serial, duration_s: float) -> None:
    end = time.time() + duration_s
    while time.time() < end:
        chunk = _read(ser, 256)
        if chunk:
            sys.stdout.write(chunk.decode(errors="replace"))
            sys.stdout.flush()


def main() -> None:
    args = parse_args()
    commands = build_commands(args)

    port = find_port()
    with open_serial(port) as ser:
        print("[prov] opened — probing for console")
        ser.write(b"help\r")
        if not wait_for(ser, SHELL_PROMPT, timeout_s=5.0):
            print("\n[prov] no shell prompt — reconnect the C6 and retry")
            return

        for cmd in commands:
            print(f"\n[prov] >> {cmd}")
            ser.write(cmd.encode() + b"\r")
            wait_for(ser, SHELL_PROMPT, timeout_s=3.0)

        print("\n[prov] provisioned — tailing serial for 60s")
        drain(ser, 60.0)


if __name__ == "__main__":
    main()
