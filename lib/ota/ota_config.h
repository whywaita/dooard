#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#ifndef DOOARD_FIRMWARE_VERSION
#define DOOARD_FIRMWARE_VERSION "0.0.0-dev"
#endif

namespace dooard {
namespace ota {

struct Version {
  uint32_t major = 0;
  uint32_t minor = 0;
  uint32_t patch = 0;
};

constexpr const char *kFirmwareVersion = DOOARD_FIRMWARE_VERSION;
constexpr const char *kOtaManifestUrl =
    "https://whywaita.github.io/dooard/firmware/version.json";
constexpr const char *kDefaultFirmwareUrl =
    "https://whywaita.github.io/dooard/firmware/firmware.bin";
constexpr uint32_t kOtaPollIntervalMs = 6UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kOtaManualHoldMs = 2UL * 1000UL;
constexpr uint32_t kOtaHttpTimeoutMs = 20UL * 1000UL;
constexpr size_t kOtaDownloadBufferSize = 4096;

const char *currentFirmwareVersion();
bool parseVersion(const char *text, Version &out);
int compareVersions(const Version &left, const Version &right);
int compareVersions(const char *left, const char *right);
bool isNewerVersion(const char *current, const char *candidate);
bool isSha256Hex(const std::string &value);

} // namespace ota
} // namespace dooard
