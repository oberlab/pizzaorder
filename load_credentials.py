#!/usr/bin/env python3
"""
Load WIFI_SSID and WIFI_PASSWORD from a credentials file and expose them
as environment variables. Optionally prints export lines or runs a subcommand
with the variables set.

Usage examples:
  - Just check and show what would be set:
      python3 load_credentials.py
  - Print shell export lines (use with: eval $(python3 load_credentials.py --print-exports)):
      python3 load_credentials.py --print-exports
  - Run PlatformIO build with env set:
      python3 load_credentials.py --run pio run

The credentials file format (default: ./credentials.txt):
  WIFI_SSID=YourWifiName
  WIFI_PASSWORD=YourSecretPassword

Lines starting with '#' are ignored. Empty lines are ignored.
"""

import os
import shlex
import subprocess
import sys
from typing import Dict


def load_credentials(path: str) -> Dict[str, str]:
    if not os.path.exists(path):
        return {}
    result: Dict[str, str] = {}
    with open(path, "r", encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if "=" not in line:
                # skip malformed lines silently
                continue
            key, val = line.split("=", 1)
            key = key.strip()
            val = val.strip()
            if key in ("WIFI_SSID", "WIFI_PASSWORD"):
                result[key] = val
    return result


def mask(value: str, keep: int = 2) -> str:
    if value is None:
        return ""
    if len(value) <= keep:
        return "*" * len(value)
    return value[:keep] + "*" * (len(value) - keep)


def print_help() -> None:
    print(__doc__.strip())


def main(argv: list[str]) -> int:
    import argparse

    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--file", default="credentials.txt", help="Pfad zur credentials-Datei")
    parser.add_argument("--print-exports", action="store_true", help="Export-Zeilen ausgeben")
    parser.add_argument("--print-defines", action="store_true", help="PlatformIO -D Defines ausgeben")
    parser.add_argument("--run", nargs=argparse.REMAINDER, help="Kommando mit gesetzten ENV ausführen")
    parser.add_argument("-h", "--help", action="store_true", help="Hilfe anzeigen")

    ns = parser.parse_args(argv)

    if ns.help:
        print_help()
        return 0

    creds = load_credentials(ns.file)
    if not creds:
        print(f"Keine credentials gefunden unter: {ns.file}")
        return 0

    ssid = creds.get("WIFI_SSID")
    pwd = creds.get("WIFI_PASSWORD")

    if ssid:
        os.environ["WIFI_SSID"] = ssid
    if pwd:
        os.environ["WIFI_PASSWORD"] = pwd

    if ns.print_exports:
        if ssid:
            print(f"export WIFI_SSID={shlex.quote(ssid)}")
        if pwd:
            print(f"export WIFI_PASSWORD={shlex.quote(pwd)}")
        return 0

    if ns.print_defines:
        # Ausgabe für platformio.ini: build_flags = !python3 load_credentials.py --print-defines
        if ssid:
            print(f"-DWIFI_SSID=\\\"{ssid}\\\"")
        if pwd:
            print(f"-DWIFI_PASSWORD=\\\"{pwd}\\\"")
        return 0

    if ns.run:
        # Drop the leading '--' if present
        cmd = ns.run
        if cmd and cmd[0] == "--":
            cmd = cmd[1:]
        if not cmd:
            print("--run ohne Kommando verwendet.")
            return 2
        try:
            return subprocess.call(cmd, env=os.environ.copy())
        except FileNotFoundError:
            print(f"Kommando nicht gefunden: {cmd[0]}")
            return 127

    # Default: show summary
    print("Umgebungsvariablen gesetzt:")
    if ssid:
        print(f"  WIFI_SSID: {ssid}")
    else:
        print("  WIFI_SSID: (nicht gesetzt)")
    if pwd:
        print(f"  WIFI_PASSWORD: {mask(pwd)}")
    else:
        print("  WIFI_PASSWORD: (nicht gesetzt)")
    print("Hinweis: Für den Shell-Export: eval $(python3 load_credentials.py --print-exports)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
