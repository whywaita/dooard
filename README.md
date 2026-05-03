# dooard

M5Stack Core2 weather display for "what's left today".

## Features

- Shows current weather plus the rest-of-today summary
- Refreshes on a timer
- Refreshes on button press
- Build-time location config
- Private-first development flow

## Setup

1. Install `uv`
2. Run `make sync`
3. Copy `include/secrets.example.h` to `include/secrets.local.h`
4. Fill in Wi-Fi credentials
5. Adjust latitude / longitude / label for your location
6. Build with PlatformIO for `m5stack-core2`

## Tooling

- `make sync` creates the local Python environment and installs PlatformIO
- `make test` runs docs checks and native C++ tests with PlatformIO
- `make lint` runs `pio check`
- `make build` runs `pio run` inside that environment
- `make monitor` opens the serial monitor

## Notes

- Wi-Fi credentials are kept out of git via `secrets.local.h`
- Weather data uses Open-Meteo
- Power behavior and measurement guidance for battery deployments lives in
  [Power notes for M5Stack Core2](docs/power.md)
