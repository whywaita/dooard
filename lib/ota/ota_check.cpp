#include "ota_check.h"

#include <ArduinoJson.h>

#include "ota_config.h"

#if defined(ARDUINO)
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif

namespace dooard {
namespace ota {
namespace {

bool isHttpsUrl(const char *url) {
  return url != nullptr && std::string(url).rfind("https://", 0) == 0;
}

} // namespace

bool parseManifestJson(const char *json, OtaManifest &out, std::string &error) {
  JsonDocument doc;
  const DeserializationError jsonError = deserializeJson(doc, json);
  if (jsonError) {
    error = std::string("JSON ") + jsonError.c_str();
    return false;
  }

  const char *version = doc["version"] | "";
  const char *firmwareUrl = doc["firmware_url"] | "";
  const char *sha256 = doc["sha256"] | "";

  Version parsedVersion;
  if (!parseVersion(version, parsedVersion)) {
    error = "invalid version";
    return false;
  }
  if (!isHttpsUrl(firmwareUrl)) {
    error = "firmware_url must be https://";
    return false;
  }
  if (!isSha256Hex(std::string(sha256))) {
    error = "invalid sha256";
    return false;
  }

  OtaManifest parsed;
  parsed.version = version;
  parsed.firmware_url = firmwareUrl;
  parsed.sha256 = sha256;
  parsed.size_bytes = doc["size_bytes"] | 0U;
  out = parsed;
  error.clear();
  return true;
}

#if defined(ARDUINO)
bool fetchOtaManifest(OtaManifest &out, std::string &error) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(kOtaHttpTimeoutMs);
  if (!http.begin(client, kOtaManifestUrl)) {
    error = "HTTP begin failed";
    return false;
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    error = "HTTP " + std::to_string(httpCode);
    http.end();
    return false;
  }

  const String body = http.getString();
  http.end();
  return parseManifestJson(body.c_str(), out, error);
}
#endif

} // namespace ota
} // namespace dooard
