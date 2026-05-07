#pragma once

#include <cstdint>

namespace dooard {

constexpr const char *kWeatherApiBaseUrl =
    "http://api.open-meteo.com/v1/forecast";
constexpr uint32_t kWeatherRefreshIntervalMs = 30UL * 60UL * 1000UL;
constexpr uint32_t kDimTimeoutMs = 2UL * 60UL * 1000UL;
constexpr uint32_t kSleepTimeoutMs = 5UL * 60UL * 1000UL;
constexpr uint8_t kActiveBrightness = 128;
constexpr uint8_t kDimBrightness = 16;
constexpr uint8_t kSleepBrightness = kDimBrightness;
constexpr uint32_t kActiveCpuFrequencyMhz = 240;
constexpr uint32_t kIdleCpuFrequencyMhz = 80;

constexpr uint8_t kButtonAGpio = 37;
constexpr uint8_t kButtonBGpio = 38;
constexpr uint8_t kButtonCGpio = 39;
constexpr uint8_t kTouchIntrGpio = 36;
constexpr uint32_t kButtonReleasePollMs = 20;
constexpr uint32_t kButtonReleaseSettleMs = 100;
constexpr const char *kButtonWakeTitle = "Refreshing";
constexpr const char *kButtonWakeReason = "Button pressed";

enum class PowerStage : uint8_t {
  kActive,
  kDim,
  kSleep,
};

constexpr uint64_t weatherRefreshIntervalUs() {
  return static_cast<uint64_t>(kWeatherRefreshIntervalMs) * 1000ULL;
}

constexpr uint32_t inactivityBeforeSleepMs() {
  return kDimTimeoutMs + kSleepTimeoutMs;
}

constexpr PowerStage stageAfterInactivity(uint32_t inactivityMs) {
  return inactivityMs >= inactivityBeforeSleepMs()
             ? PowerStage::kSleep
             : (inactivityMs >= kDimTimeoutMs ? PowerStage::kDim
                                              : PowerStage::kActive);
}

constexpr uint8_t brightnessForStage(PowerStage stage) {
  return stage == PowerStage::kActive
             ? kActiveBrightness
             : (stage == PowerStage::kDim ? kDimBrightness : kSleepBrightness);
}

constexpr uint32_t sleepWakeIntervalMs(uint32_t elapsedSinceWeatherRefreshMs) {
  return elapsedSinceWeatherRefreshMs >= kWeatherRefreshIntervalMs
             ? 0
             : ((kWeatherRefreshIntervalMs - elapsedSinceWeatherRefreshMs) <
                        kSleepTimeoutMs
                    ? (kWeatherRefreshIntervalMs - elapsedSinceWeatherRefreshMs)
                    : kSleepTimeoutMs);
}

constexpr uint64_t sleepWakeIntervalUs(uint32_t elapsedSinceWeatherRefreshMs) {
  return static_cast<uint64_t>(
             sleepWakeIntervalMs(elapsedSinceWeatherRefreshMs)) *
         1000ULL;
}

constexpr uint64_t buttonWakeupMask() {
  return (1ULL << kButtonAGpio) | (1ULL << kButtonBGpio) |
         (1ULL << kButtonCGpio);
}

constexpr bool isUserInteractionWakeup(bool touchWakeup, bool ext1ButtonWakeup,
                                       bool gpioButtonWakeup) {
  return touchWakeup || ext1ButtonWakeup || gpioButtonWakeup;
}

} // namespace dooard
