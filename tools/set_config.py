#!/usr/bin/env python3
"""
Publish a retained MQTT config message to a sensor-tag-wifi device.

The tag picks up the retained message on its next wake's subscribe, applies
the changes to NVS, and acks on `combsense/hive/<id>/config/ack`. Subsequent
wakes are no-ops because the tag's idempotency check sees the values already
match.

Allowed keys (firmware v1):
  --sample-int   30..3600 sec (wake interval)
  --upload-every 1..60 (drain frequency)
  --tag-name     string ≤63 chars (human label)
  --ota-host     IPv4 string (OTA endpoint, no scheme)

Usage:
  python3 tools/set_config.py --tag c5127f04 --sample-int 300

Optional:
  --host  MQTT broker host (default: 192.168.1.82)
  --port  MQTT broker port (default: 1883)
  --user  MQTT username (default: hivesense)
  --pass  MQTT password (prompts if not given)
  --watch-ack  subscribe to config/ack and print the response (default: true)
  --clear      publish empty retained message to clear pending config
"""

from __future__ import annotations

import argparse
import getpass
import json
import sys
import time

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("error: paho-mqtt not installed. install with: pip install paho-mqtt",
          file=sys.stderr)
    sys.exit(1)


def build_payload(args) -> dict:
    payload = {}
    if args.sample_int is not None:
        payload["sample_int"] = args.sample_int
    if args.upload_every is not None:
        payload["upload_every"] = args.upload_every
    if args.tag_name is not None:
        payload["tag_name"] = args.tag_name
    if args.ota_host is not None:
        payload["ota_host"] = args.ota_host
    return payload


def main() -> int:
    p = argparse.ArgumentParser(
        description="Publish a retained MQTT config message to a sensor-tag-wifi tag.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("--tag", required=True,
                   help="device-id (8 hex chars, e.g. c5127f04)")

    p.add_argument("--sample-int", type=int,
                   help="seconds between samples, 30..3600")
    p.add_argument("--upload-every", type=int,
                   help="drain every N samples, 1..60")
    p.add_argument("--tag-name", help="human label, ≤63 chars")
    p.add_argument("--ota-host", help="IPv4 string, no scheme (e.g. 192.168.1.61)")

    p.add_argument("--host", default="192.168.1.82",
                   help="MQTT broker (default: 192.168.1.82)")
    p.add_argument("--port", type=int, default=1883)
    p.add_argument("--user", default="hivesense")
    p.add_argument("--pass", dest="passwd",
                   help="MQTT password (prompts if not provided)")
    p.add_argument("--watch-ack", action="store_true", default=True,
                   help="subscribe to config/ack and print response (default)")
    p.add_argument("--no-watch-ack", dest="watch_ack", action="store_false",
                   help="don't wait for the ack")
    p.add_argument("--clear", action="store_true",
                   help="clear the retained config message instead of setting it")

    args = p.parse_args()

    if args.passwd is None:
        args.passwd = getpass.getpass(f"MQTT password for {args.user}: ")

    config_topic = f"combsense/hive/{args.tag}/config"
    ack_topic    = f"combsense/hive/{args.tag}/config/ack"

    if args.clear:
        payload_str = ""
        print(f"[set_config] clearing retained message at {config_topic}")
    else:
        payload = build_payload(args)
        if not payload:
            print("error: provide at least one of --sample-int / --upload-every / "
                  "--tag-name / --ota-host (or use --clear)", file=sys.stderr)
            return 2
        payload_str = json.dumps(payload, separators=(",", ":"))
        print(f"[set_config] publishing retained to {config_topic}")
        print(f"[set_config] payload: {payload_str}")

    received_ack: dict = {}

    def on_connect(client, userdata, flags, rc, properties=None):
        if rc != 0:
            print(f"[set_config] MQTT connect failed rc={rc}", file=sys.stderr)
            client.disconnect()
            return
        if args.watch_ack and not args.clear:
            client.subscribe(ack_topic, qos=0)

    def on_message(client, userdata, msg):
        if msg.topic == ack_topic:
            try:
                received_ack.update(json.loads(msg.payload.decode("utf-8")))
            except Exception as e:
                print(f"[set_config] ack parse failed: {e}", file=sys.stderr)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(args.user, args.passwd)
    client.on_connect = on_connect
    client.on_message = on_message

    client.connect(args.host, args.port, keepalive=30)
    client.loop_start()

    # Publish the retained config (or empty payload to clear).
    info = client.publish(config_topic, payload_str, qos=0, retain=True)
    info.wait_for_publish(timeout=10)

    if not args.watch_ack or args.clear:
        client.loop_stop()
        client.disconnect()
        print("[set_config] done — config published" +
              (" (cleared)" if args.clear else ""))
        return 0

    # Wait up to ~70s for the ack (covers a 60s wake cycle plus margin).
    deadline = time.time() + 70
    print(f"[set_config] waiting up to 70s for ack on {ack_topic} ...")
    while time.time() < deadline:
        if received_ack:
            break
        time.sleep(1)

    client.loop_stop()
    client.disconnect()

    if not received_ack:
        print("[set_config] no ack received within 70s. the tag may be in a long "
              "sleep cycle, or the ack may arrive later.", file=sys.stderr)
        return 1

    print("[set_config] ack received:")
    print(json.dumps(received_ack, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
