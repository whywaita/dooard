#pragma once

#include <cstdint>

namespace dooard {

constexpr const char *kWeatherApiBaseUrl =
    "http://api.open-meteo.com/v1/forecast";
constexpr uint32_t kWeatherRefreshIntervalMs = 30UL * 60UL * 1000UL;
constexpr uint8_t kDisplayBrightness = 64;
constexpr uint32_t kActiveCpuFrequencyMhz = 240;
constexpr uint32_t kIdleCpuFrequencyMhz = 80;

constexpr uint8_t kButtonAGpio = 37;
constexpr uint8_t kButtonBGpio = 38;
constexpr uint8_t kButtonCGpio = 39;
constexpr const char *kButtonWakeTitle = "Refreshing";
constexpr const char *kButtonWakeReason = "Button pressed";

constexpr uint64_t weatherRefreshIntervalUs() {
  return static_cast<uint64_t>(kWeatherRefreshIntervalMs) * 1000ULL;
}

constexpr uint64_t buttonWakeupMask() {
  return (1ULL << kButtonAGpio) | (1ULL << kButtonBGpio) |
         (1ULL << kButtonCGpio);
}

} // namespace dooard
