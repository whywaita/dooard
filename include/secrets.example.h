#pragma once

// Copy to secrets.local.h and keep it untracked.
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Optional API settings. Runtime values are read from NVS when configured.
#define DOOARD_API_KEY ""
#define DOOARD_DEVICE_ID "dooard-core2"
#define DOOARD_ENDPOINT_URL "http://api.open-meteo.com/v1/forecast"

// Build-time location config.
#define WEATHER_LATITUDE 35.681236
#define WEATHER_LONGITUDE 139.767125
#define WEATHER_LABEL "Tokyo Station"
