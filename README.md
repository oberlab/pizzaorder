**PizzaOrder ESP32**

- **Ziel:** ESP32-Projekt mit WLAN-Zugangsdaten aus `credentials.txt` bauen.
- **Build-System:** PlatformIO (Arduino-Framework)

**Setup**

- **Credentials-Datei:** Lege im Projekt-Root eine `credentials.txt` an (wird nicht committed):
  - `WIFI_SSID=DeinWLAN`
  - `WIFI_PASSWORD=DeinPasswort`
- **Ignoriert von Git:** `credentials.txt` ist in `.gitignore` eingetragen.

**Wie es funktioniert**

- `platformio.ini` bezieht die WLAN-Daten über das Python-Script:
  - `build_flags = !python3 load_credentials.py --print-defines`
- Das Script `load_credentials.py` liest `credentials.txt` und gibt passende `-D` Defines aus, z. B.:
  - `-DWIFI_SSID="MeinWLAN"`
  - `-DWIFI_PASSWORD="SehrGeheimesPasswort"`
- Im Code (`src/main.cpp`) werden diese Defines als `WIFI_SSID` und `WIFI_PASSWORD` verwendet; fehlen sie, greifen Fallback-Werte.

**Build & Upload**

- **Build:** `pio run`
- **Serieller Monitor:** `pio device monitor`
- **Upload (USB):** `pio run -t upload`
- **Upload (OTA):** Nutze das `esp32dev-ota` Environment (IP ggf. in `platformio.ini` setzen):
  - `pio run -e esp32dev-ota -t upload`

**Alternativ: Shell-Exports**

- Falls du die Umgebungsvariablen manuell setzen möchtest:
  - `eval $(python3 load_credentials.py --print-exports)`
  - Danach: `pio run`

**Hinweise**

- **Format von `credentials.txt`:** `KEY=VALUE`, leere Zeilen und `#`-Kommentare sind erlaubt.
- **Sicherheit:** Lege echte Zugangsdaten nur lokal ab; `credentials.txt` wird nicht versioniert.
- **Python-Kommando:** Falls dein System `python3` nicht kennt, passe den Aufruf in `platformio.ini` an (z. B. `python`).

