#include "ota_update.h"

#if defined(ARDUINO)

#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClient.h>
#include <algorithm>
#include <cctype>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>

#include "ota_config.h"

namespace dooard {
namespace ota {
namespace {

int sha256Starts(mbedtls_sha256_context &ctx) {
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
  return mbedtls_sha256_starts(&ctx, 0);
#else
  return mbedtls_sha256_starts_ret(&ctx, 0);
#endif
}

int sha256Update(mbedtls_sha256_context &ctx, const unsigned char *data,
                 size_t len) {
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
  return mbedtls_sha256_update(&ctx, data, len);
#else
  return mbedtls_sha256_update_ret(&ctx, data, len);
#endif
}

int sha256Finish(mbedtls_sha256_context &ctx, unsigned char digest[32]) {
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
  return mbedtls_sha256_finish(&ctx, digest);
#else
  return mbedtls_sha256_finish_ret(&ctx, digest);
#endif
}

std::string sha256Hex(const unsigned char digest[32]) {
  constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (size_t i = 0; i < 32; ++i) {
    out.push_back(kHex[(digest[i] >> 4) & 0x0F]);
    out.push_back(kHex[digest[i] & 0x0F]);
  }
  return out;
}

std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

} // namespace

bool performOtaUpdate(const OtaManifest &manifest, std::string &error) {
  if (!isSha256Hex(manifest.sha256)) {
    error = "invalid sha256";
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(kOtaHttpTimeoutMs);
  if (!http.begin(client, manifest.firmware_url.c_str())) {
    error = "HTTP begin failed";
    return false;
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    error = "HTTP " + std::to_string(httpCode);
    http.end();
    return false;
  }

  const int contentLength = http.getSize();
  if (contentLength <= 0) {
    error = "missing Content-Length";
    http.end();
    return false;
  }
  if (manifest.size_bytes != 0U &&
      static_cast<size_t>(contentLength) != manifest.size_bytes) {
    error = "size mismatch";
    http.end();
    return false;
  }
  if (!Update.begin(static_cast<size_t>(contentLength))) {
    error = Update.errorString();
    http.end();
    return false;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  if (sha256Starts(sha) != 0) {
    mbedtls_sha256_free(&sha);
    Update.abort();
    http.end();
    error = "SHA256 init failed";
    return false;
  }

  uint8_t buffer[kOtaDownloadBufferSize];
  WiFiClient *stream = http.getStreamPtr();
  int remaining = contentLength;
  size_t totalWritten = 0;
  unsigned long lastReadAt = millis();
  while (http.connected() && remaining > 0) {
    const size_t available = stream->available();
    if (available == 0U) {
      if (millis() - lastReadAt > kOtaHttpTimeoutMs) {
        mbedtls_sha256_free(&sha);
        Update.abort();
        http.end();
        error = "download timeout";
        return false;
      }
      delay(1);
      continue;
    }

    const size_t toRead = std::min(
        available, std::min(sizeof(buffer), static_cast<size_t>(remaining)));
    const size_t bytesRead = stream->readBytes(buffer, toRead);
    if (bytesRead == 0U) {
      continue;
    }
    lastReadAt = millis();

    if (sha256Update(sha, buffer, bytesRead) != 0) {
      mbedtls_sha256_free(&sha);
      Update.abort();
      http.end();
      error = "SHA256 update failed";
      return false;
    }
    if (Update.write(buffer, bytesRead) != bytesRead) {
      mbedtls_sha256_free(&sha);
      error = Update.errorString();
      Update.abort();
      http.end();
      return false;
    }

    totalWritten += bytesRead;
    remaining -= static_cast<int>(bytesRead);
  }

  unsigned char digest[32];
  if (sha256Finish(sha, digest) != 0) {
    mbedtls_sha256_free(&sha);
    Update.abort();
    http.end();
    error = "SHA256 finish failed";
    return false;
  }
  mbedtls_sha256_free(&sha);
  http.end();

  if (totalWritten != static_cast<size_t>(contentLength)) {
    Update.abort();
    error = "download incomplete";
    return false;
  }
  if (sha256Hex(digest) != lowerAscii(manifest.sha256)) {
    Update.abort();
    error = "sha256 mismatch";
    return false;
  }
  if (!Update.end(true) || !Update.isFinished()) {
    error = Update.errorString();
    return false;
  }

  error.clear();
  return true;
}

} // namespace ota
} // namespace dooard

#endif
