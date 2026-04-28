# dooard

M5Stack Core2 weather display for "what's left today".

## Features

- Shows current weather plus the rest-of-today summary
- Refreshes on a timer
- Refreshes on button press
- Build-time location config
- Private-first development flow

## Setup

1. Copy `include/secrets.example.h` to `include/secrets.local.h`
2. Fill in Wi-Fi credentials
3. Adjust latitude / longitude / label for your location
4. Build with PlatformIO for `m5stack-core2`

## Notes

- Wi-Fi credentials are kept out of git via `secrets.local.h`
- Weather data uses Open-Meteo
