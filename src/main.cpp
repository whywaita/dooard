#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <cstring>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <string>
#include <time.h>
#include <vector>

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

#include "credential_store.h"
#include "ota_check.h"
#include "ota_config.h"
#include "ota_update.h"
#include "power_policy.h"
#include "weather_logic.h"

#ifndef CORE2_APP_NAME
#define CORE2_APP_NAME "dooard"
#endif

#ifndef DOOARD_API_KEY
#define DOOARD_API_KEY ""
#endif

#ifndef DOOARD_DEVICE_ID
#define DOOARD_DEVICE_ID "dooard-core2"
#endif

#ifndef DOOARD_ENDPOINT_URL
#define DOOARD_ENDPOINT_URL "http://api.open-meteo.com/v1/forecast"
#endif

namespace {
constexpr const char *kTimeZone = "JST-9";
constexpr const char *kNtp1 = "ntp.nict.jp";
constexpr const char *kNtp2 = "pool.ntp.org";
constexpr unsigned long kWifiConnectTimeoutMs = 20000;
constexpr unsigned long kClockSyncIntervalMs = 6UL * 60UL * 60UL * 1000UL;
constexpr const char *kWifiSsidPlaceholder = "YOUR_WIFI_SSID";
constexpr const char *kWifiPasswordPlaceholder = "YOUR_WIFI_PASSWORD";

struct WeatherState {
  bool ok = false;
  String title;
  String summary;
  String updatedAt;
  float currentTemperature = NAN;
  int currentWeatherCode = -1;
  std::vector<dooard::ThreeHourBucket> buckets;
};

struct AppCredentialSettings {
  bool valid = false;
  bool fromNvs = false;
  std::string wifiSsid;
  std::string wifiPassword;
  std::string apiKey;
  std::string deviceId;
  std::string endpointUrl;
};

constexpr size_t kBucketCount = 6;

WeatherState state;
AppCredentialSettings credentialSettings;
unsigned long lastWeatherFetch = 0;
unsigned long lastClockSync = 0;
unsigned long lastInteractionAt = 0;
unsigned long lastOtaCheck = 0;
unsigned long otaButtonHoldStartedAt = 0;
dooard::PowerStage powerStage = dooard::PowerStage::kActive;
esp_sleep_wakeup_cause_t pendingWakeupCause = ESP_SLEEP_WAKEUP_UNDEFINED;
dooard::ota::OtaManifest pendingOtaManifest;
bool pendingOtaAvailable = false;
bool otaButtonActionTaken = false;
String otaStatusLine;

String formatNowLabel();
void drawLoading(const String &line1, const String &line2 = "");
void drawState(const WeatherState &s, bool wifiOk);
bool loadCredentialSettings();
bool loadStoredCredentialSettings(
    AppCredentialSettings &out, dooard::credentials::CredentialStatus &status);
bool loadFallbackCredentialSettings(AppCredentialSettings &out);
bool runSerialCredentialSetup();
void applyCredentialRecord(const dooard::credentials::CredentialRecord &record,
                           bool fromNvs, AppCredentialSettings &out);
std::string readSerialCredentialLine(const char *prompt, bool required);
bool missingOrPlaceholder(const char *value, const char *placeholder);
bool ensureWifi();
bool ensureClock();
bool fetchWeather(WeatherState &out);
String buildWeatherUrl();
bool refreshWeather(bool buttonWake);
bool checkForOtaUpdate(bool showProgress);
void executeOtaUpdate();
void noteUserActivity();
void applyPowerStage(dooard::PowerStage nextStage);
void updatePowerStageForInactivity(unsigned long now);
esp_sleep_wakeup_cause_t consumeWakeupCause();
bool isButtonWakeup(esp_sleep_wakeup_cause_t wakeupCause);
bool isTimerWakeup(esp_sleep_wakeup_cause_t wakeupCause);
bool buttonsPressed();
bool otaButtonChordPressed();
bool handleOtaButtonChord();
bool buttonsHeldLow();
void waitForButtonsReleased();
bool weatherRefreshDue(unsigned long now);
bool otaPollDue(unsigned long now);
uint32_t elapsedSinceWeatherRefresh(unsigned long now);
uint64_t nextSleepTimerUs(unsigned long now);
void setActiveCpuFrequency();
void setIdleCpuFrequency();
void shutdownWifi();
void configureWakeupSources(uint64_t timerUs);
void enterLightSleep();
std::vector<dooard::HourlyForecast>
parseHourlyForecasts(const JsonObject &hourly);
} // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  Serial.setTimeout(300000);
  setActiveCpuFrequency();
  applyPowerStage(dooard::PowerStage::kActive);
  pinMode(dooard::kButtonAGpio, INPUT);
  pinMode(dooard::kButtonBGpio, INPUT);
  pinMode(dooard::kButtonCGpio, INPUT);
  pinMode(dooard::kTouchIntrGpio, INPUT);
  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(1);

  drawLoading(CORE2_APP_NAME, "Loading credentials...");
  if (!loadCredentialSettings()) {
    while (true) {
      M5.update();
      delay(100);
    }
  }

  drawLoading(CORE2_APP_NAME, "Connecting Wi-Fi...");

  state.title = WEATHER_LABEL;
  state.summary = "Loading";
  lastInteractionAt = millis();

  refreshWeather(false);
  checkForOtaUpdate(false);
  shutdownWifi();
  setIdleCpuFrequency();
}

void loop() {
  M5.update();
  const esp_sleep_wakeup_cause_t wakeupCause = consumeWakeupCause();
  if (handleOtaButtonChord()) {
    delay(50);
    return;
  }

  const bool buttonRefresh = isButtonWakeup(wakeupCause) || buttonsPressed();

  if (buttonRefresh) {
    noteUserActivity();
    refreshWeather(true);
    shutdownWifi();
    setIdleCpuFrequency();
    return;
  }

  const unsigned long now = millis();
  const bool timerWake = isTimerWakeup(wakeupCause);
  if (timerWake && powerStage == dooard::PowerStage::kSleep) {
    applyPowerStage(dooard::PowerStage::kDim);
  }

  if (weatherRefreshDue(now)) {
    if (timerWake) {
      applyPowerStage(dooard::PowerStage::kDim);
    }
    refreshWeather(false);
    if (powerStage != dooard::PowerStage::kActive) {
      applyPowerStage(dooard::PowerStage::kDim);
    }
    shutdownWifi();
  }

  if (otaPollDue(millis())) {
    if (timerWake) {
      applyPowerStage(dooard::PowerStage::kDim);
    }
    checkForOtaUpdate(false);
    shutdownWifi();
  }

  updatePowerStageForInactivity(millis());
  if (powerStage == dooard::PowerStage::kSleep) {
    enterLightSleep();
    return;
  }

  shutdownWifi();
  setIdleCpuFrequency();
  delay(100);
}

namespace {
void setActiveCpuFrequency() {
  setCpuFrequencyMhz(dooard::kActiveCpuFrequencyMhz);
}

void setIdleCpuFrequency() { setCpuFrequencyMhz(dooard::kIdleCpuFrequencyMhz); }

void applyCredentialRecord(const dooard::credentials::CredentialRecord &record,
                           bool fromNvs, AppCredentialSettings &out) {
  out.valid = true;
  out.fromNvs = fromNvs;
  out.wifiSsid = record.wifi_ssid;
  out.wifiPassword = record.wifi_password;
  out.apiKey = record.api_key;
  out.deviceId = record.device_id;
  out.endpointUrl = record.endpoint_url;
}

bool loadStoredCredentialSettings(
    AppCredentialSettings &out, dooard::credentials::CredentialStatus &status) {
  dooard::credentials::CredentialStore store;
  if (!store.begin(dooard::credentials::CredentialAccessMode::kReadOnly)) {
    status = store.lastStatus();
    return false;
  }

  dooard::credentials::CredentialRecord record;
  const bool loaded = store.load(record);
  status = store.lastStatus();
  store.end();
  if (!loaded) {
    return false;
  }

  applyCredentialRecord(record, true, out);
  return true;
}

bool missingOrPlaceholder(const char *value, const char *placeholder) {
  return value == nullptr || value[0] == '\0' ||
         std::strcmp(value, placeholder) == 0;
}

bool loadFallbackCredentialSettings(AppCredentialSettings &out) {
  if (missingOrPlaceholder(WIFI_SSID, kWifiSsidPlaceholder) ||
      missingOrPlaceholder(WIFI_PASSWORD, kWifiPasswordPlaceholder)) {
    return false;
  }

  dooard::credentials::CredentialRecord record;
  record.wifi_ssid = WIFI_SSID;
  record.wifi_password = WIFI_PASSWORD;
  record.api_key = DOOARD_API_KEY;
  record.device_id = DOOARD_DEVICE_ID;
  record.endpoint_url = DOOARD_ENDPOINT_URL;
  dooard::credentials::finalizeCredentialRecord(record);
  if (dooard::credentials::validateCredentialRecord(record) !=
      dooard::credentials::CredentialStatus::kOk) {
    return false;
  }

  applyCredentialRecord(record, false, out);
  return true;
}

bool loadCredentialSettings() {
  dooard::credentials::CredentialStatus storedStatus =
      dooard::credentials::CredentialStatus::kStorageUnavailable;
  if (loadStoredCredentialSettings(credentialSettings, storedStatus)) {
    Serial.println("Loaded credentials from NVS namespace dooard-creds.");
    return true;
  }

  Serial.print("Stored credentials status: ");
  Serial.println(dooard::credentials::credentialStatusText(storedStatus));

  if (storedStatus != dooard::credentials::CredentialStatus::kNotConfigured &&
      storedStatus !=
          dooard::credentials::CredentialStatus::kStorageUnavailable) {
    drawLoading(
        "Credentials invalid",
        String(dooard::credentials::credentialStatusText(storedStatus)));
    delay(1500);
    return runSerialCredentialSetup();
  }

  if (loadFallbackCredentialSettings(credentialSettings)) {
    Serial.println("Using build-time credential fallback.");
    return true;
  }

  drawLoading("First setup", "Open serial monitor");
  return runSerialCredentialSetup();
}

std::string readSerialCredentialLine(const char *prompt, bool required) {
  while (true) {
    Serial.print(prompt);
    Serial.print(required ? " (required): " : " (optional): ");
    while (!Serial.available()) {
      M5.update();
      delay(50);
    }

    String value = Serial.readStringUntil('\n');
    value.trim();
    if (!value.isEmpty() || !required) {
      return std::string(value.c_str());
    }
    Serial.println("Value is required.");
  }
}

bool runSerialCredentialSetup() {
  while (Serial.available()) {
    Serial.read();
  }

  Serial.println();
  Serial.println("dooard credential setup");
  Serial.println("Values are stored in NVS namespace dooard-creds.");
  Serial.println(
      "API key may be empty when the configured endpoint does not need one.");

  dooard::credentials::CredentialRecord record;
  record.wifi_ssid = readSerialCredentialLine("WiFi SSID", true);
  record.wifi_password = readSerialCredentialLine("WiFi password", true);
  record.api_key = readSerialCredentialLine("API key", false);
  record.device_id = readSerialCredentialLine("Device ID", false);
  if (record.device_id.empty()) {
    record.device_id = DOOARD_DEVICE_ID;
    Serial.print("Using default device_id: ");
    Serial.println(record.device_id.c_str());
  }
  record.endpoint_url = readSerialCredentialLine("Endpoint URL", false);
  if (record.endpoint_url.empty()) {
    record.endpoint_url = DOOARD_ENDPOINT_URL;
    Serial.print("Using default endpoint_url: ");
    Serial.println(record.endpoint_url.c_str());
  }

  dooard::credentials::CredentialStore store;
  if (!store.begin(dooard::credentials::CredentialAccessMode::kReadWrite)) {
    Serial.println("NVS credential store is unavailable.");
    drawLoading("Setup failed", "NVS unavailable");
    return false;
  }

  const bool saved = store.save(record);
  const dooard::credentials::CredentialStatus status = store.lastStatus();
  store.end();
  if (!saved) {
    Serial.print("Credential save failed: ");
    Serial.println(dooard::credentials::credentialStatusText(status));
    drawLoading("Setup failed",
                String(dooard::credentials::credentialStatusText(status)));
    return false;
  }

  Serial.println("Credentials saved. Restarting...");
  drawLoading("Setup complete", "Restarting...");
  delay(1500);
  ESP.restart();
  return true;
}

bool ensureWifi() {
  if (!credentialSettings.valid) {
    return false;
  }

  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.begin(credentialSettings.wifiSsid.c_str(),
             credentialSettings.wifiPassword.c_str());
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < kWifiConnectTimeoutMs) {
    M5.update();
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool ensureClock() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  configTzTime(kTimeZone, kNtp1, kNtp2);
  struct tm timeinfo{};
  if (getLocalTime(&timeinfo, 5000)) {
    lastClockSync = millis();
    return true;
  }
  return false;
}

String buildWeatherUrl() {
  const char *baseUrl = credentialSettings.endpointUrl.empty()
                            ? dooard::kWeatherApiBaseUrl
                            : credentialSettings.endpointUrl.c_str();
  String url =
      String(baseUrl) + "?latitude=" + String(WEATHER_LATITUDE, 6) +
      "&longitude=" + String(WEATHER_LONGITUDE, 6) + "&timezone=Asia%2FTokyo" +
      "&forecast_days=2" +
      "&current=temperature_2m,weather_code,precipitation_probability" +
      "&hourly=temperature_2m,weather_code,precipitation_probability";
  return url;
}

bool fetchWeather(WeatherState &out) {
  out.ok = false;
  out.title = WEATHER_LABEL;
  out.summary = "No data";
  out.buckets.clear();

  if (WiFi.status() != WL_CONNECTED) {
    out.summary = "Wi-Fi disconnected";
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, buildWeatherUrl())) {
    out.summary = "HTTP begin failed";
    return false;
  }
  http.useHTTP10(true);
  if (!credentialSettings.apiKey.empty()) {
    http.addHeader("Authorization",
                   String("Bearer ") + credentialSettings.apiKey.c_str());
  }
  if (!credentialSettings.deviceId.empty()) {
    http.addHeader("X-Dooard-Device-Id", credentialSettings.deviceId.c_str());
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

  struct tm nowTm{};
  if (!getLocalTime(&nowTm, 1000)) {
    out.summary =
        String(dooard::weatherCodeText(out.currentWeatherCode).c_str());
    out.updatedAt = "--:--";
    out.ok = true;
    return true;
  }

  char currentHourLabel[16];
  strftime(currentHourLabel, sizeof(currentHourLabel), "%Y-%m-%dT%H", &nowTm);
  const std::vector<dooard::HourlyForecast> hourlyForecasts =
      parseHourlyForecasts(hourly);
  out.buckets = dooard::buildThreeHourBuckets(hourlyForecasts, currentHourLabel,
                                              kBucketCount);

  out.summary = String(dooard::weatherCodeText(out.currentWeatherCode).c_str());
  out.updatedAt = formatNowLabel();
  out.ok = true;
  return true;
}

bool refreshWeather(bool buttonWake) {
  setActiveCpuFrequency();
  if (buttonWake) {
    drawLoading(dooard::kButtonWakeTitle, dooard::kButtonWakeReason);
  }

  ensureWifi();
  if (lastClockSync == 0 || millis() - lastClockSync > kClockSyncIntervalMs) {
    ensureClock();
  }

  const bool updated = fetchWeather(state);
  lastWeatherFetch = millis();
  drawState(state, WiFi.status() == WL_CONNECTED);
  setIdleCpuFrequency();
  return updated;
}

bool checkForOtaUpdate(bool showProgress) {
  setActiveCpuFrequency();
  if (showProgress) {
    drawLoading("OTA check", "Connecting...");
  }

  if (!ensureWifi()) {
    lastOtaCheck = millis();
    otaStatusLine = "OTA Wi-Fi NG";
    if (showProgress) {
      drawLoading("OTA failed", "Wi-Fi disconnected");
      delay(2000);
    }
    drawState(state, WiFi.status() == WL_CONNECTED);
    setIdleCpuFrequency();
    return false;
  }

  if (showProgress) {
    drawLoading("OTA check", "Fetching version");
  }

  dooard::ota::OtaManifest manifest;
  std::string error;
  const bool fetched = dooard::ota::fetchOtaManifest(manifest, error);
  lastOtaCheck = millis();
  if (!fetched) {
    pendingOtaAvailable = false;
    otaStatusLine = "OTA check failed";
    if (showProgress) {
      drawLoading("OTA failed", String(error.c_str()));
      delay(2500);
    }
    drawState(state, WiFi.status() == WL_CONNECTED);
    setIdleCpuFrequency();
    return false;
  }

  if (dooard::ota::isNewerVersion(dooard::ota::currentFirmwareVersion(),
                                  manifest.version.c_str())) {
    pendingOtaManifest = manifest;
    pendingOtaAvailable = true;
    otaStatusLine = String("OTA ") + manifest.version.c_str() + " hold A+B+C";
  } else {
    pendingOtaAvailable = false;
    otaStatusLine = "";
  }

  if (showProgress && !pendingOtaAvailable) {
    drawLoading("OTA check", "No update");
    delay(1500);
  }
  drawState(state, WiFi.status() == WL_CONNECTED);
  setIdleCpuFrequency();
  return pendingOtaAvailable;
}

void executeOtaUpdate() {
  noteUserActivity();
  drawLoading("OTA update", "Preparing...");

  // Always re-poll the manifest so a server-side release published after the
  // last cache update does not cause size/sha256 verification failures with
  // stale cached values.
  if (!checkForOtaUpdate(true)) {
    shutdownWifi();
    return;
  }
  if (!pendingOtaAvailable) {
    drawLoading("OTA update", "No update");
    delay(1500);
    drawState(state, WiFi.status() == WL_CONNECTED);
    shutdownWifi();
    return;
  }

  if (!dooard::ota::isNewerVersion(dooard::ota::currentFirmwareVersion(),
                                   pendingOtaManifest.version.c_str())) {
    pendingOtaAvailable = false;
    otaStatusLine = "";
    drawLoading("OTA update", "No update");
    delay(1500);
    drawState(state, WiFi.status() == WL_CONNECTED);
    shutdownWifi();
    return;
  }

  if (!ensureWifi()) {
    otaStatusLine = "OTA Wi-Fi NG";
    drawLoading("OTA failed", "Wi-Fi disconnected");
    delay(2500);
    drawState(state, WiFi.status() == WL_CONNECTED);
    shutdownWifi();
    return;
  }

  drawLoading("OTA update",
              String("Downloading ") + pendingOtaManifest.version.c_str());
  std::string error;
  if (!dooard::ota::performOtaUpdate(pendingOtaManifest, error)) {
    otaStatusLine = "OTA failed";
    drawLoading("OTA failed", String(error.c_str()));
    delay(3000);
    drawState(state, WiFi.status() == WL_CONNECTED);
    shutdownWifi();
    return;
  }

  drawLoading("OTA complete", "Restarting...");
  delay(1500);
  ESP.restart();
}

void noteUserActivity() {
  lastInteractionAt = millis();
  applyPowerStage(dooard::PowerStage::kActive);
}

void applyPowerStage(dooard::PowerStage nextStage) {
  powerStage = nextStage;
  M5.Display.setBrightness(dooard::brightnessForStage(nextStage));
}

void updatePowerStageForInactivity(unsigned long now) {
  const uint32_t inactivityMs = static_cast<uint32_t>(now - lastInteractionAt);
  const dooard::PowerStage nextStage =
      dooard::stageAfterInactivity(inactivityMs);
  if (nextStage != powerStage) {
    applyPowerStage(nextStage);
  }
}

esp_sleep_wakeup_cause_t consumeWakeupCause() {
  const esp_sleep_wakeup_cause_t wakeupCause = pendingWakeupCause;
  pendingWakeupCause = ESP_SLEEP_WAKEUP_UNDEFINED;
  return wakeupCause;
}

bool isButtonWakeup(esp_sleep_wakeup_cause_t wakeupCause) {
  return dooard::isUserInteractionWakeup(wakeupCause == ESP_SLEEP_WAKEUP_EXT0,
                                         wakeupCause == ESP_SLEEP_WAKEUP_EXT1,
                                         wakeupCause == ESP_SLEEP_WAKEUP_GPIO);
}

bool isTimerWakeup(esp_sleep_wakeup_cause_t wakeupCause) {
  return wakeupCause == ESP_SLEEP_WAKEUP_TIMER;
}

bool buttonsPressed() {
  return M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed();
}

bool otaButtonChordPressed() {
  return digitalRead(dooard::kButtonAGpio) == LOW &&
         digitalRead(dooard::kButtonBGpio) == LOW &&
         digitalRead(dooard::kButtonCGpio) == LOW;
}

bool handleOtaButtonChord() {
  if (!otaButtonChordPressed()) {
    otaButtonHoldStartedAt = 0;
    otaButtonActionTaken = false;
    return false;
  }

  if (otaButtonHoldStartedAt == 0) {
    otaButtonHoldStartedAt = millis();
    noteUserActivity();
  }

  if (!otaButtonActionTaken &&
      static_cast<uint32_t>(millis() - otaButtonHoldStartedAt) >=
          dooard::ota::kOtaManualHoldMs) {
    otaButtonActionTaken = true;
    executeOtaUpdate();
  }
  return true;
}

bool buttonsHeldLow() {
  return digitalRead(dooard::kButtonAGpio) == LOW ||
         digitalRead(dooard::kButtonBGpio) == LOW ||
         digitalRead(dooard::kButtonCGpio) == LOW;
}

void waitForButtonsReleased() {
  for (;;) {
    while (buttonsHeldLow()) {
      M5.update();
      delay(dooard::kButtonReleasePollMs);
    }
    delay(dooard::kButtonReleaseSettleMs);
    if (!buttonsHeldLow()) {
      return;
    }
  }
}

bool weatherRefreshDue(unsigned long now) {
  return lastWeatherFetch == 0 ||
         static_cast<uint32_t>(now - lastWeatherFetch) >=
             dooard::kWeatherRefreshIntervalMs;
}

bool otaPollDue(unsigned long now) {
  return lastOtaCheck == 0 || static_cast<uint32_t>(now - lastOtaCheck) >=
                                  dooard::ota::kOtaPollIntervalMs;
}

uint32_t elapsedSinceWeatherRefresh(unsigned long now) {
  if (lastWeatherFetch == 0) {
    return 0;
  }
  return static_cast<uint32_t>(now - lastWeatherFetch);
}

uint64_t nextSleepTimerUs(unsigned long now) {
  const uint32_t timerMs =
      dooard::sleepWakeIntervalMs(elapsedSinceWeatherRefresh(now));
  if (timerMs == 0) {
    return dooard::sleepWakeIntervalUs(0);
  }
  return static_cast<uint64_t>(timerMs) * 1000ULL;
}

void shutdownWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void configureWakeupSources(uint64_t timerUs) {
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_timer_wakeup(timerUs);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

#if CONFIG_IDF_TARGET_ESP32
  esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(dooard::kTouchIntrGpio),
                               0);
  gpio_wakeup_enable(static_cast<gpio_num_t>(dooard::kButtonAGpio),
                     GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(static_cast<gpio_num_t>(dooard::kButtonBGpio),
                     GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(static_cast<gpio_num_t>(dooard::kButtonCGpio),
                     GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
#else
  esp_sleep_enable_ext1_wakeup(dooard::buttonWakeupMask(),
                               ESP_EXT1_WAKEUP_ANY_LOW);
#endif
}

void enterLightSleep() {
  applyPowerStage(dooard::PowerStage::kSleep);
  shutdownWifi();
  setIdleCpuFrequency();
  waitForButtonsReleased();
  configureWakeupSources(nextSleepTimerUs(millis()));
  esp_light_sleep_start();
  pendingWakeupCause = esp_sleep_get_wakeup_cause();
}

String formatNowLabel() {
  struct tm nowTm{};
  if (!getLocalTime(&nowTm, 1000)) {
    return "--:--";
  }
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &nowTm);
  return String(buf);
}

void drawLoading(const String &line1, const String &line2) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(line1, M5.Display.width() / 2,
                        M5.Display.height() / 2 - 16);
  if (!line2.isEmpty()) {
    M5.Display.drawString(line2, M5.Display.width() / 2,
                          M5.Display.height() / 2 + 16);
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
  M5.Display.fillTriangle(cx - 1, cy + 8, cx + 5, cy + 8, cx - 3, cy + 14,
                          TFT_YELLOW);
  M5.Display.fillTriangle(cx + 1, cy + 12, cx + 6, cy + 12, cx, cy + 20,
                          TFT_YELLOW);
}

void drawFogIcon(int cx, int cy) {
  for (int i = -1; i <= 1; ++i) {
    M5.Display.drawLine(cx - 13, cy + i * 6, cx + 13, cy + i * 6,
                        TFT_LIGHTGREY);
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
  if (probability >= 70)
    return TFT_RED;
  if (probability >= 40)
    return TFT_YELLOW;
  return TFT_WHITE;
}

void drawState(const WeatherState &s, bool wifiOk) {
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
    M5.Display.drawString(
        String("Now ") + String(s.currentTemperature, 1) + "C", 314, 18);
  } else {
    M5.Display.drawString(s.summary, 314, 18);
  }

  M5.Display.drawFastHLine(0, 30, 320, TFT_DARKGREY);

  if (s.buckets.empty()) {
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(2);
    M5.Display.drawString(s.summary.isEmpty() ? "No forecast" : s.summary, 160,
                          120);
  } else {
    constexpr int kCellTop = 36;
    constexpr int kCellBottom = 200;
    constexpr int kCellWidth = 320 / 6;
    for (size_t i = 0; i < s.buckets.size() && i < kBucketCount; ++i) {
      const auto &b = s.buckets[i];
      const int cx = (int)i * kCellWidth + kCellWidth / 2;

      if (i > 0) {
        M5.Display.drawFastVLine((int)i * kCellWidth, kCellTop,
                                 kCellBottom - kCellTop, TFT_DARKGREY);
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
      M5.Display.setTextColor(rainColor(b.max_precipitation_probability),
                              TFT_BLACK);
      const String pctText = b.max_precipitation_probability >= 0
                                 ? String(b.max_precipitation_probability) + "%"
                                 : String("--");
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
  M5.Display.drawString(
      otaStatusLine.isEmpty() ? "Btn A/B/C: refresh" : otaStatusLine, 314, 218);
}

std::vector<dooard::HourlyForecast>
parseHourlyForecasts(const JsonObject &hourly) {
  std::vector<dooard::HourlyForecast> forecasts;
  const JsonArray times = hourly["time"].as<JsonArray>();
  const JsonArray temps = hourly["temperature_2m"].as<JsonArray>();
  const JsonArray probs = hourly["precipitation_probability"].as<JsonArray>();
  const JsonArray codes = hourly["weather_code"].as<JsonArray>();

  const size_t count = times.size();
  forecasts.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    dooard::HourlyForecast forecast;
    forecast.time = (const char *)(times[i] | "");
    forecast.temperature = temps[i] | NAN;
    forecast.precipitation_probability = probs[i] | -1;
    forecast.weather_code = codes[i] | -1;
    forecasts.push_back(forecast);
  }
  return forecasts;
}
} // namespace
