# dooard

M5Stack Core2 weather display for "what's left today".

## Features

- Shows current weather plus the rest-of-today summary
- Refreshes on a timer
- Refreshes on button press
- Build-time location config
- HTTP polling OTA firmware updates
- Private-first development flow

## Setup

1. Install `uv`
2. Run `make sync`
3. Copy `include/secrets.example.h` to `include/secrets.local.h`
4. Fill in Wi-Fi credentials
5. Adjust latitude / longitude / label for your location
6. Build with PlatformIO for `m5stack-core2`

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

- Wi-Fi credentials are kept out of git via `secrets.local.h`
- Weather data uses Open-Meteo
