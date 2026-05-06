# dooard

M5Stack Core2 weather display for "what's left today".

## Features

- Shows current weather plus the rest-of-today summary
- Refreshes on a timer
- Refreshes on button press
- Build-time location config
- Runtime credential storage in ESP32 NVS
- HTTP polling OTA firmware updates
- Private-first development flow

## Setup

1. Install `uv`
2. Run `make sync`
3. Adjust latitude / longitude / label for your location
4. Build with PlatformIO for `m5stack-core2`

Build-time credentials remain supported as a migration fallback:

1. Copy `include/secrets.example.h` to `include/secrets.local.h`
2. Fill in `WIFI_SSID`, `WIFI_PASSWORD`, `DOOARD_API_KEY`,
   `DOOARD_DEVICE_ID`, and `DOOARD_ENDPOINT_URL`
3. Build and flash normally

When NVS credentials are present, they take precedence over these build-time
values.

## Initial credential setup

On boot, dooard reads credentials from ESP32 NVS namespace `dooard-creds`.
Stored records include a configured flag, schema version, and checksum. If the
record is missing and no build-time fallback exists, or if the stored record is
corrupt, the device enters serial setup mode.

Open the serial monitor at 115200 baud:

```sh
make monitor
```

Enter the prompted values:

- WiFi SSID
- WiFi password
- API key, optional for endpoints that do not need one
- Device ID
- Endpoint URL

The firmware writes the values to NVS, marks the record configured, and restarts.
Every later boot, including the first boot after OTA, validates all credential
keys before Wi-Fi or HTTP access. Corrupt or incompatible records show an error
on the display and return to serial setup.

## OTA firmware updates

Firmware builds use the `default_8MB.csv` partition table so the Core2 has OTA
app slots. The firmware version is compiled from `DOOARD_FIRMWARE_VERSION` in
`platformio.ini`.

The device polls this manifest every 6 hours, independently from weather data:

```text
http://whywaita.github.io/dooard/firmware/version.json
```

The manifest format is:

```json
{
  "version": "0.1.0",
  "firmware_url": "http://whywaita.github.io/dooard/firmware/firmware.bin",
  "sha256": "<64 lowercase hex chars>",
  "size_bytes": 1234
}
```

When a newer version is available, the display footer shows the available OTA
version. Hold buttons A+B+C together for 2 seconds to download and apply the
update. The downloaded firmware is written only after its SHA256 matches the
manifest.

Pushing a tag such as `v0.1.0` builds `.pio/build/core2/firmware.bin`, writes
`firmware/version.json`, and deploys both files to GitHub Pages.

## NVS, Flash Encryption, and Secure Boot

Release devices store Wi-Fi credentials, API key, device ID, and endpoint URL in
NVS instead of firmware. The existing `default_8MB.csv` partition table already
contains the 24 KB `nvs` partition at `0x9000`, and `platformio.ini` sets
`board_build.flash_mode = qio` for the Core2 firmware.

Use `core2` for development builds and `core2_secure` for release artifacts:

```sh
make build
UV_CACHE_DIR=/tmp/uv-cache UV_PYTHON_INSTALL_DIR=/tmp/uv-python \
  PLATFORMIO_CORE_DIR=/tmp/platformio-core pio run -e core2_secure
```

Flash Encryption release provisioning, Secure Boot v2 key handling, encrypted
flashing, readout comparison, and backup/recovery rules are documented in
`security/README.md`. After Flash Encryption is enabled, serial flashing must
use encrypted artifacts, for example:

```sh
pio run -e core2_secure -t encrypt -t upload
```

or `esptool.py write_flash --encrypt` with the generated encrypted binaries.

## Tooling

- `make sync` creates the local Python environment and installs PlatformIO
- `make test` runs native C++ tests with PlatformIO
- `make lint` runs `pio check`
- `make build` runs `pio run` inside that environment
- `make monitor` opens the serial monitor

For local OTA validation, run:

- `make test`
- `make build`
- `actionlint .github/workflows/*.yml`
- `pinact run -u`

## Notes

- Wi-Fi credentials are kept out of git via NVS or `secrets.local.h`
- Release key files under `security/` are ignored by git and must be backed up
  outside the repository
- Weather data uses Open-Meteo
