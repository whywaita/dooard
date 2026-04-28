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
constexpr const char* kTimeZone = "Asia/Tokyo";
constexpr const char* kNtp1 = "ntp.nict.jp";
constexpr const char* kNtp2 = "pool.ntp.org";
constexpr unsigned long kWifiConnectTimeoutMs = 20000;
constexpr unsigned long kWeatherRefreshIntervalMs = 15UL * 60UL * 1000UL;
constexpr unsigned long kClockSyncIntervalMs = 6UL * 60UL * 60UL * 1000UL;

struct WeatherState {
  bool ok = false;
  String title;
  String summary;
  String details;
  String updatedAt;
  float currentTemperature = NAN;
  int currentWeatherCode = -1;
  int currentPrecipitationProbability = -1;
  int maxRemainingRainChance = -1;
  String nextRainAt;
};

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
               "&forecast_days=1" +
               "&current=temperature_2m,weather_code,precipitation_probability" +
               "&hourly=temperature_2m,weather_code,precipitation_probability";
  return url;
}

bool fetchWeather(WeatherState& out) {
  out.ok = false;
  out.title = WEATHER_LABEL;
  out.summary = "No data";
  out.details = "";
  out.nextRainAt = "";
  out.maxRemainingRainChance = -1;

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
  out.currentPrecipitationProbability = current["precipitation_probability"] | -1;

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
  const dooard::RemainingForecastSummary summary =
      dooard::summarizeRemainingHours(hourlyForecasts, currentHourLabel, 50);

  out.maxRemainingRainChance = summary.max_rain_chance;
  out.nextRainAt = summary.next_rain_at.empty() ? String("No rain expected")
                                                : (String("Rain from ") + summary.next_rain_at.c_str());
  out.summary = String(dooard::weatherCodeText(out.currentWeatherCode).c_str());
  out.details = String(dooard::formatTempRange(summary.min_temperature, summary.max_temperature).c_str());
  out.updatedAt = formatNowLabel();
  out.ok = true;

  if (!summary.has_data) {
    out.details = "No remaining hourly data";
  }
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

void drawState(const WeatherState& s, bool wifiOk) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextSize(1);

  M5.Display.drawString(String(CORE2_APP_NAME), 10, 8);
  M5.Display.drawString(wifiOk ? "Wi-Fi OK" : "Wi-Fi NG", 220, 8);
  M5.Display.drawString(s.title, 10, 30);

  M5.Display.setTextSize(2);
  if (s.ok && !isnan(s.currentTemperature)) {
    M5.Display.drawString(String(s.currentTemperature, 1) + " C", 10, 60);
  } else {
    M5.Display.drawString(s.summary, 10, 60);
  }

  M5.Display.setTextSize(1);
  M5.Display.drawString(String("Now: ") + s.summary, 10, 110);
  M5.Display.drawString(String("Today: ") + s.details, 10, 132);
  M5.Display.drawString(
      String("Rain: ") + (s.maxRemainingRainChance >= 0 ? String(s.maxRemainingRainChance) + "%" : String("--")), 10,
      154);
  M5.Display.drawString(s.nextRainAt, 10, 176);
  M5.Display.drawString(String("Updated: ") + s.updatedAt, 10, 208);

  M5.Display.drawString("Btn A/B/C: refresh", 200, 208);
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
