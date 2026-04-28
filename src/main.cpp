#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "secrets.local.h"

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
constexpr int kRainThreshold = 50;

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
  float minRemainingTemperature = NAN;
  float maxRemainingTemperature = NAN;
  String nextRainAt;
};

WeatherState state;
unsigned long lastWeatherFetch = 0;
unsigned long lastClockSync = 0;
unsigned long lastWifiAttempt = 0;

String weatherCodeText(int code);
String formatNowLabel();
String formatDateTimeLabel(const char* timeString);
String formatTempRange(float minTemp, float maxTemp);
String hourLabelFromIso(const char* iso);
void drawLoading(const String& line1, const String& line2 = "");
void drawState(const WeatherState& s, bool wifiOk);
bool ensureWifi();
bool ensureClock();
bool fetchWeather(WeatherState& out);
String buildWeatherUrl();
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
  out.minRemainingTemperature = NAN;
  out.maxRemainingTemperature = NAN;

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

  const JsonArray times = hourly["time"].as<JsonArray>();
  const JsonArray temps = hourly["temperature_2m"].as<JsonArray>();
  const JsonArray probs = hourly["precipitation_probability"].as<JsonArray>();
  const JsonArray codes = hourly["weather_code"].as<JsonArray>();

  struct tm nowTm {};
  if (!getLocalTime(&nowTm, 1000)) {
    out.summary = weatherCodeText(out.currentWeatherCode);
    out.updatedAt = "--:--";
    out.ok = true;
    return true;
  }

  char currentHourLabel[16];
  strftime(currentHourLabel, sizeof(currentHourLabel), "%Y-%m-%dT%H", &nowTm);
  bool foundRemaining = false;

  float minTemp = NAN;
  float maxTemp = NAN;
  int maxRainChance = -1;
  String nextRainAt;

  const size_t count = times.size();
  for (size_t i = 0; i < count; ++i) {
    const char* timeString = times[i] | "";
    if (strncmp(timeString, currentHourLabel, 13) < 0) {
      continue;
    }
    const float temp = temps[i] | NAN;
    const int rainChance = probs[i] | -1;
    const int code = codes[i] | -1;

    if (!isnan(temp)) {
      if (isnan(minTemp) || temp < minTemp) minTemp = temp;
      if (isnan(maxTemp) || temp > maxTemp) maxTemp = temp;
    }
    if (rainChance > maxRainChance) {
      maxRainChance = rainChance;
    }

    const bool isRainy = rainChance >= kRainThreshold || code == 51 || code == 53 || code == 55 ||
                         code == 61 || code == 63 || code == 65 || code == 80 || code == 81 ||
                         code == 82;
    if (nextRainAt.isEmpty() && isRainy) {
      nextRainAt = hourLabelFromIso(timeString);
    }
    foundRemaining = true;
  }

  out.maxRemainingRainChance = maxRainChance;
  out.minRemainingTemperature = minTemp;
  out.maxRemainingTemperature = maxTemp;
  out.nextRainAt = nextRainAt.isEmpty() ? String("No rain expected") : (String("Rain from ") + nextRainAt);
  out.summary = weatherCodeText(out.currentWeatherCode);
  out.details = formatTempRange(out.minRemainingTemperature, out.maxRemainingTemperature);
  out.updatedAt = formatNowLabel();
  out.ok = true;

  if (!foundRemaining) {
    out.details = "No remaining hourly data";
  }
  return true;
}

String weatherCodeText(int code) {
  switch (code) {
    case 0: return "Clear";
    case 1:
    case 2:
    case 3: return "Cloudy";
    case 45:
    case 48: return "Fog";
    case 51:
    case 53:
    case 55: return "Drizzle";
    case 61:
    case 63:
    case 65:
    case 80:
    case 81:
    case 82: return "Rain";
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86: return "Snow";
    default: return code >= 0 ? String("Code ") + code : String("Unknown");
  }
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

String hourLabelFromIso(const char* timeString) {
  if (timeString == nullptr || strlen(timeString) < 13) {
    return "--:--";
  }
  char buf[6];
  buf[0] = timeString[11];
  buf[1] = timeString[12];
  buf[2] = ':';
  buf[3] = '0';
  buf[4] = '0';
  buf[5] = '\0';
  return String(buf);
}

String formatDateTimeLabel(const char* timeString) {
  if (timeString == nullptr) {
    return "--:--";
  }
  return String(timeString).substring(11, 16);
}

String formatTempRange(float minTemp, float maxTemp) {
  if (isnan(minTemp) || isnan(maxTemp)) {
    return "Temp range unavailable";
  }
  return String("Range ") + String(minTemp, 1) + ".." + String(maxTemp, 1) + " C";
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
  M5.Display.drawString("Now: " + s.summary, 10, 110);
  M5.Display.drawString("Today: " + s.details, 10, 132);
  M5.Display.drawString("Rain: " + (s.maxRemainingRainChance >= 0 ? String(s.maxRemainingRainChance) + "%" : String("--")), 10, 154);
  M5.Display.drawString(s.nextRainAt, 10, 176);
  M5.Display.drawString("Updated: " + s.updatedAt, 10, 208);

  M5.Display.drawString("Btn A/B/C: refresh", 200, 208);
}
}  // namespace
