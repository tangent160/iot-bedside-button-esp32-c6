"""PlatformIO pre-script: inject WiFi credentials from .env as build defines."""
import os

Import("env")  # noqa: F821

env_path = os.path.join(env.subst("$PROJECT_DIR"), ".env")  # noqa: F821
if not os.path.isfile(env_path):
    raise SystemExit(
        "\nMissing .env file. Copy .env.example to .env and set WIFI_SSID / WIFI_PASSWORD.\n"
    )

values = {}
with open(env_path) as f:
    for line in f:
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, val = line.partition("=")
        values[key.strip()] = val.strip().strip('"').strip("'")

missing = [k for k in ("WIFI_SSID", "WIFI_PASSWORD") if not values.get(k)]
if missing:
    raise SystemExit(f"\n.env is missing required keys: {', '.join(missing)}\n")

for key in ("WIFI_SSID", "WIFI_PASSWORD"):
    env.Append(CPPDEFINES=[(key, env.StringifyMacro(values[key]))])  # noqa: F821
