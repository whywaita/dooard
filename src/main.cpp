#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <cstring>
#include <vector>

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

#include "weather_logic.h"

#ifndef CORE2_APP_NAME
#define CORE2_APP_NAME "dooard"
#endif

namespace {
constexpr const char* kTimeZone = "JST-9";
constexpr const char* kNtp1 = "ntp.nict.jp";
constexpr const char* kNtp2 = "pool.ntp.org";
constexpr unsigned long kWifiConnectTimeoutMs = 20000;
constexpr unsigned long kWeatherRefreshIntervalMs = 15UL * 60UL * 1000UL;
constexpr unsigned long kClockSyncIntervalMs = 6UL * 60UL * 60UL * 1000UL;

struct WeatherState {
  bool ok = false;
  String title;
  String summary;
  String updatedAt;
  float currentTemperature = NAN;
  int currentWeatherCode = -1;
  std::vector<dooard::ThreeHourBucket> buckets;
};

constexpr size_t kBucketCount = 6;

WeatherState state;
unsigned long lastWeatherFetch = 0;
unsigned long lastClockSync = 0;
unsigned long lastWifiAttempt = 0;

String formatNowLabel();
void drawLoading(const String& line1, const String& line2 = "");
void drawState(const WeatherState& s, bool wifiOk);
bool ensureWifi();
bool ensureClock();
bool fetchWeather(WeatherState& out);
String buildWeatherUrl();
std::vector<dooard::HourlyForecast> parseHourlyForecasts(const JsonObject& hourly);
}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(1);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  drawLoading(CORE2_APP_NAME, "Connecting Wi-Fi...");
  ensureWifi();
  ensureClock();

  state.title = WEATHER_LABEL;
  state.summary = "Loading";
  drawState(state, WiFi.status() == WL_CONNECTED);

  if (fetchWeather(state)) {
    lastWeatherFetch = millis();
  }
  drawState(state, WiFi.status() == WL_CONNECTED);
}

void loop() {
  M5.update();

  const bool buttonRefresh = M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed();
  if (buttonRefresh) {
    drawLoading("Refreshing", "Button pressed");
    ensureWifi();
    ensureClock();
    if (fetchWeather(state)) {
      lastWeatherFetch = millis();
    }
    drawState(state, WiFi.status() == WL_CONNECTED);
  }

  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiAttempt > 10000UL) {
    ensureWifi();
  }

  if (millis() - lastClockSync > kClockSyncIntervalMs) {
    ensureClock();
  }

  if (millis() - lastWeatherFetch > kWeatherRefreshIntervalMs || lastWeatherFetch == 0) {
    if (WiFi.status() == WL_CONNECTED) {
      if (fetchWeather(state)) {
        lastWeatherFetch = millis();
      }
      drawState(state, WiFi.status() == WL_CONNECTED);
    }
  }

  delay(50);
}

namespace {
bool ensureWifi() {
  lastWifiAttempt = millis();
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  WiFi.reconnect();
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiConnectTimeoutMs) {
    M5.update();
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool ensureClock() {
  configTzTime(kTimeZone, kNtp1, kNtp2);
  struct tm timeinfo {};
  if (getLocalTime(&timeinfo, 5000)) {
    lastClockSync = millis();
    return true;
  }
  return false;
}

String buildWeatherUrl() {
  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + String(WEATHER_LATITUDE, 6) +
               "&longitude=" + String(WEATHER_LONGITUDE, 6) +
               "&timezone=Asia%2FTokyo" +
               "&forecast_days=2" +
               "&current=temperature_2m,weather_code,precipitation_probability" +
               "&hourly=temperature_2m,weather_code,precipitation_probability";
  return url;
}

bool fetchWeather(WeatherState& out) {
  out.ok = false;
  out.title = WEATHER_LABEL;
  out.summary = "No data";
  out.buckets.clear();

  if (WiFi.status() != WL_CONNECTED) {
    out.summary = "Wi-Fi disconnected";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, buildWeatherUrl())) {
    out.summary = "HTTP begin failed";
    return false;
  }
  http.useHTTP10(true);

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    out.summary = String("HTTP ") + httpCode;
    http.end();
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    out.summary = String("JSON ") + err.c_str();
    return false;
  }

  const JsonObject current = doc["current"].as<JsonObject>();
  const JsonObject hourly = doc["hourly"].as<JsonObject>();
  if (current.isNull() || hourly.isNull()) {
    out.summary = "Missing weather payload";
    return false;
  }

  out.currentTemperature = current["temperature_2m"] | NAN;
  out.currentWeatherCode = current["weather_code"] | -1;

  struct tm nowTm {};
  if (!getLocalTime(&nowTm, 1000)) {
    out.summary = String(dooard::weatherCodeText(out.currentWeatherCode).c_str());
    out.updatedAt = "--:--";
    out.ok = true;
    return true;
  }

  char currentHourLabel[16];
  strftime(currentHourLabel, sizeof(currentHourLabel), "%Y-%m-%dT%H", &nowTm);
  const std::vector<dooard::HourlyForecast> hourlyForecasts = parseHourlyForecasts(hourly);
  out.buckets = dooard::buildThreeHourBuckets(hourlyForecasts, currentHourLabel, kBucketCount);

  out.summary = String(dooard::weatherCodeText(out.currentWeatherCode).c_str());
  out.updatedAt = formatNowLabel();
  out.ok = true;
  return true;
}

String formatNowLabel() {
  struct tm nowTm {};
  if (!getLocalTime(&nowTm, 1000)) {
    return "--:--";
  }
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &nowTm);
  return String(buf);
}

void drawLoading(const String& line1, const String& line2) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(line1, M5.Display.width() / 2, M5.Display.height() / 2 - 16);
  if (!line2.isEmpty()) {
    M5.Display.drawString(line2, M5.Display.width() / 2, M5.Display.height() / 2 + 16);
  }
}

void drawCloudShape(int cx, int cy, uint16_t color) {
  M5.Display.fillCircle(cx - 9, cy + 2, 7, color);
  M5.Display.fillCircle(cx + 9, cy + 2, 7, color);
  M5.Display.fillCircle(cx, cy - 4, 9, color);
  M5.Display.fillRect(cx - 14, cy + 2, 28, 6, color);
}

void drawSunIcon(int cx, int cy) {
  const uint16_t color = TFT_YELLOW;
  M5.Display.fillCircle(cx, cy, 7, color);
  for (int a = 0; a < 8; ++a) {
    const float rad = a * (PI / 4.0f);
    const int x1 = cx + (int)(cosf(rad) * 11);
    const int y1 = cy + (int)(sinf(rad) * 11);
    const int x2 = cx + (int)(cosf(rad) * 16);
    const int y2 = cy + (int)(sinf(rad) * 16);
    M5.Display.drawLine(x1, y1, x2, y2, color);
  }
}

void drawPartlyCloudyIcon(int cx, int cy) {
  M5.Display.fillCircle(cx - 6, cy - 6, 5, TFT_YELLOW);
  for (int a = 0; a < 8; ++a) {
    const float rad = a * (PI / 4.0f);
    const int x1 = cx - 6 + (int)(cosf(rad) * 8);
    const int y1 = cy - 6 + (int)(sinf(rad) * 8);
    const int x2 = cx - 6 + (int)(cosf(rad) * 11);
    const int y2 = cy - 6 + (int)(sinf(rad) * 11);
    M5.Display.drawLine(x1, y1, x2, y2, TFT_YELLOW);
  }
  drawCloudShape(cx + 2, cy + 4, TFT_LIGHTGREY);
}

void drawCloudIcon(int cx, int cy, uint16_t tint) {
  drawCloudShape(cx, cy, tint);
}

void drawRainIcon(int cx, int cy, uint16_t cloudTint) {
  drawCloudShape(cx, cy - 4, cloudTint);
  for (int i = -1; i <= 1; ++i) {
    const int x = cx + i * 7;
    M5.Display.drawLine(x + 1, cy + 9, x - 2, cy + 16, TFT_CYAN);
    M5.Display.drawLine(x + 2, cy + 9, x - 1, cy + 16, TFT_CYAN);
  }
}

void drawSnowIcon(int cx, int cy) {
  drawCloudShape(cx, cy - 4, TFT_LIGHTGREY);
  for (int i = -1; i <= 1; ++i) {
    const int x = cx + i * 7;
    const int y = cy + 13;
    M5.Display.drawLine(x - 2, y, x + 2, y, TFT_WHITE);
    M5.Display.drawLine(x, y - 2, x, y + 2, TFT_WHITE);
    M5.Display.drawLine(x - 1, y - 1, x + 1, y + 1, TFT_WHITE);
    M5.Display.drawLine(x - 1, y + 1, x + 1, y - 1, TFT_WHITE);
  }
}

void drawThunderIcon(int cx, int cy) {
  drawCloudShape(cx, cy - 4, TFT_DARKGREY);
  M5.Display.fillTriangle(cx - 1, cy + 8, cx + 5, cy + 8, cx - 3, cy + 14, TFT_YELLOW);
  M5.Display.fillTriangle(cx + 1, cy + 12, cx + 6, cy + 12, cx, cy + 20, TFT_YELLOW);
}

void drawFogIcon(int cx, int cy) {
  for (int i = -1; i <= 1; ++i) {
    M5.Display.drawLine(cx - 13, cy + i * 6, cx + 13, cy + i * 6, TFT_LIGHTGREY);
  }
}

void drawWeatherIcon(int cx, int cy, int code) {
  if (code == 0) {
    drawSunIcon(cx, cy);
  } else if (code == 1 || code == 2) {
    drawPartlyCloudyIcon(cx, cy);
  } else if (code == 3) {
    drawCloudIcon(cx, cy, TFT_LIGHTGREY);
  } else if (code == 45 || code == 48) {
    drawFogIcon(cx, cy);
  } else if (code >= 51 && code <= 55) {
    drawRainIcon(cx, cy, TFT_LIGHTGREY);
  } else if ((code >= 61 && code <= 65) || (code >= 80 && code <= 82)) {
    drawRainIcon(cx, cy, TFT_DARKGREY);
  } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
    drawSnowIcon(cx, cy);
  } else if (code >= 95) {
    drawThunderIcon(cx, cy);
  } else {
    drawCloudIcon(cx, cy, TFT_LIGHTGREY);
  }
}

uint16_t rainColor(int probability) {
  if (probability >= 70) return TFT_RED;
  if (probability >= 40) return TFT_YELLOW;
  return TFT_WHITE;
}

void drawState(const WeatherState& s, bool wifiOk) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(top_left);
  M5.Display.drawString(String(CORE2_APP_NAME), 6, 6);
  M5.Display.drawString(s.title, 70, 6);

  M5.Display.setTextDatum(top_right);
  M5.Display.setTextColor(wifiOk ? TFT_GREEN : TFT_RED, TFT_BLACK);
  M5.Display.drawString(wifiOk ? "Wi-Fi OK" : "Wi-Fi NG", 314, 6);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  if (s.ok && !isnan(s.currentTemperature)) {
    M5.Display.drawString(String("Now ") + String(s.currentTemperature, 1) + "C", 314, 18);
  } else {
    M5.Display.drawString(s.summary, 314, 18);
  }

  M5.Display.drawFastHLine(0, 30, 320, TFT_DARKGREY);

  if (s.buckets.empty()) {
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(2);
    M5.Display.drawString(s.summary.isEmpty() ? "No forecast" : s.summary, 160, 120);
  } else {
    constexpr int kCellTop = 36;
    constexpr int kCellBottom = 200;
    constexpr int kCellWidth = 320 / 6;
    for (size_t i = 0; i < s.buckets.size() && i < kBucketCount; ++i) {
      const auto& b = s.buckets[i];
      const int cx = (int)i * kCellWidth + kCellWidth / 2;

      if (i > 0) {
        M5.Display.drawFastVLine((int)i * kCellWidth, kCellTop, kCellBottom - kCellTop, TFT_DARKGREY);
      }

      M5.Display.setTextDatum(top_center);
      M5.Display.setTextFont(2);
      M5.Display.setTextSize(1);
      M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
      char hourBuf[8];
      std::snprintf(hourBuf, sizeof(hourBuf), "%02d:00", b.start_hour);
      M5.Display.drawString(hourBuf, cx, kCellTop + 4);
      M5.Display.setTextFont(0);

      drawWeatherIcon(cx, kCellTop + 56, b.worst_weather_code);

      M5.Display.setTextSize(2);
      M5.Display.setTextColor(rainColor(b.max_precipitation_probability), TFT_BLACK);
      const String pctText =
          b.max_precipitation_probability >= 0 ? String(b.max_precipitation_probability) + "%" : String("--");
      M5.Display.drawString(pctText, cx, kCellTop + 92);

      M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      char tempBuf[12];
      if (!isnan(b.start_temperature)) {
        std::snprintf(tempBuf, sizeof(tempBuf), "%.0fC", b.start_temperature);
      } else {
        std::snprintf(tempBuf, sizeof(tempBuf), "--C");
      }
      M5.Display.drawString(tempBuf, cx, kCellTop + 120);
    }
  }

  M5.Display.drawFastHLine(0, 210, 320, TFT_DARKGREY);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.setTextDatum(top_left);
  M5.Display.drawString(String("Updated ") + s.updatedAt, 6, 218);
  M5.Display.setTextDatum(top_right);
  M5.Display.drawString("Btn A/B/C: refresh", 314, 218);
}

std::vector<dooard::HourlyForecast> parseHourlyForecasts(const JsonObject& hourly) {
  std::vector<dooard::HourlyForecast> forecasts;
  const JsonArray times = hourly["time"].as<JsonArray>();
  const JsonArray temps = hourly["temperature_2m"].as<JsonArray>();
  const JsonArray probs = hourly["precipitation_probability"].as<JsonArray>();
  const JsonArray codes = hourly["weather_code"].as<JsonArray>();

  const size_t count = times.size();
  forecasts.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    dooard::HourlyForecast forecast;
    forecast.time = (const char*)(times[i] | "");
    forecast.temperature = temps[i] | NAN;
    forecast.precipitation_probability = probs[i] | -1;
    forecast.weather_code = codes[i] | -1;
    forecasts.push_back(forecast);
  }
  return forecasts;
}
}  // namespace
