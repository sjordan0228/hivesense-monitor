"""Inject FIRMWARE_VERSION as a build flag from `git describe`.

Runs as a PlatformIO pre-script (extra_scripts = pre:get_version.py). Must
be deterministic for cache hits — same git state must yield the same flag.
"""
import subprocess

Import("env")  # noqa: F821 — provided by PlatformIO


def _git_describe() -> str:
    try:
        out = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            stderr=subprocess.DEVNULL,
        )
        return out.decode("utf-8").strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "unknown"


version = _git_describe()
print(f"[get_version] FIRMWARE_VERSION={version}")
env.Append(BUILD_FLAGS=[f'-DFIRMWARE_VERSION=\\"{version}\\"'])  # noqa: F821
