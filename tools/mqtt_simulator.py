#!/usr/bin/env python3
"""
Simulates a HiveSense yard collector publishing sensor data to MQTT.
Generates realistic hive telemetry for testing the iOS app and dashboards
without physical hardware.

Usage:
    python3 mqtt_simulator.py
    python3 mqtt_simulator.py --hives 5 --interval 10
    python3 mqtt_simulator.py --host 192.168.1.82 --user hivesense --password hivesense
"""

import argparse
import math
import random
import signal
import sys
import time
from datetime import datetime

import paho.mqtt.client as mqtt

running = True


def signal_handler(sig, frame):
    global running
    print("\nShutting down...")
    running = False


class HiveSimulator:
    """Maintains state for one simulated hive node with realistic sensor drift."""

    def __init__(self, hive_id: str):
        self.id = hive_id
        self.weight = random.uniform(40.0, 90.0)
        self.battery = random.uniform(85.0, 100.0)
        self.rssi = random.randint(-65, -45)
        self.temp_internal = random.uniform(34.0, 36.0)
        self.humidity_internal = random.uniform(55.0, 70.0)
        self.humidity_external = random.uniform(35.0, 55.0)

        # Per-hive variation so not all hives behave identically
        self._weight_trend = random.uniform(0.01, 0.05)  # kg per interval (nectar flow)
        self._temp_offset = random.uniform(-1.0, 1.0)

    def update(self, now: datetime) -> dict:
        hour = now.hour + now.minute / 60.0

        # Day factor: 0 at night, peaks at 1 around solar noon (hour 13)
        day_factor = max(0.0, math.sin((hour - 6.0) * math.pi / 14.0))

        # Weight: gradual upward trend (nectar flow) with small noise
        self.weight += self._weight_trend + random.gauss(0, 0.05)
        self.weight = max(30.0, min(130.0, self.weight))

        # Internal temp: colony maintains ~35C, small variation
        self.temp_internal += random.gauss(0, 0.1)
        self.temp_internal = max(32.0, min(38.0, self.temp_internal))

        # External temp: sine wave following day/night, peaks ~2pm
        base_external = 15.0 + 15.0 * math.sin((hour - 7.0) * math.pi / 14.0)
        external_temp = base_external + self._temp_offset + random.gauss(0, 0.3)

        # Humidity
        self.humidity_internal += random.gauss(0, 0.5)
        self.humidity_internal = max(50.0, min(80.0, self.humidity_internal))
        self.humidity_external += random.gauss(0, 0.8)
        self.humidity_external = max(25.0, min(75.0, self.humidity_external))

        # Bee traffic: peaks midday, zero at night
        bees_in = int(day_factor * random.uniform(200, 500))
        bees_out = int(day_factor * random.uniform(180, 480))
        activity = int(day_factor * random.uniform(300, 800))

        # Battery: slow decline
        self.battery -= random.uniform(0.002, 0.01)
        self.battery = max(0.0, self.battery)

        # RSSI: slight variation
        rssi = self.rssi + random.randint(-3, 3)
        rssi = max(-80, min(-30, rssi))

        return {
            f"hivesense/hive/{self.id}/weight": f"{self.weight:.2f}",
            f"hivesense/hive/{self.id}/temp/internal": f"{self.temp_internal:.1f}",
            f"hivesense/hive/{self.id}/temp/external": f"{external_temp:.1f}",
            f"hivesense/hive/{self.id}/humidity/internal": f"{self.humidity_internal:.1f}",
            f"hivesense/hive/{self.id}/humidity/external": f"{self.humidity_external:.1f}",
            f"hivesense/hive/{self.id}/bees/in": str(bees_in),
            f"hivesense/hive/{self.id}/bees/out": str(bees_out),
            f"hivesense/hive/{self.id}/bees/activity": str(activity),
            f"hivesense/hive/{self.id}/battery": str(int(self.battery)),
            f"hivesense/hive/{self.id}/rssi": str(rssi),
        }


def parse_args():
    parser = argparse.ArgumentParser(
        description="Simulate HiveSense collector MQTT publishes"
    )
    parser.add_argument("--host", default="192.168.1.82")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--user", default="hivesense")
    parser.add_argument("--password", default="hivesense")
    parser.add_argument("--interval", type=int, default=30, help="Seconds between publishes")
    parser.add_argument("--hives", type=int, default=3, help="Number of simulated hives")
    return parser.parse_args()


def main():
    args = parse_args()
    signal.signal(signal.SIGINT, signal_handler)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(args.user, args.password)

    print(f"Connecting to {args.host}:{args.port}...")
    client.connect(args.host, args.port, 60)
    client.loop_start()

    hives = [HiveSimulator(f"HIVE-{i + 1:03d}") for i in range(args.hives)]
    print(f"Simulating {args.hives} hives, publishing every {args.interval}s. Ctrl+C to stop.\n")

    while running:
        now = datetime.now()
        timestamp = now.strftime("%H:%M:%S")

        for hive in hives:
            readings = hive.update(now)
            for topic, value in readings.items():
                client.publish(topic, value)

            # Print summary line per hive instead of per-topic
            print(
                f"[{timestamp}] {hive.id}: "
                f"weight={readings[f'hivesense/hive/{hive.id}/weight']}kg "
                f"temp={readings[f'hivesense/hive/{hive.id}/temp/internal']}C "
                f"batt={readings[f'hivesense/hive/{hive.id}/battery']}%"
            )

        print()
        time.sleep(args.interval)

    client.loop_stop()
    client.disconnect()
    print("Disconnected.")


if __name__ == "__main__":
    main()
