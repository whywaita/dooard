#include "weather_logic.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace dooard {
namespace {
bool isRainyWeatherCodeImpl(int code) {
  return code == 51 || code == 53 || code == 55 || code == 61 || code == 63 || code == 65 || code == 80 ||
         code == 81 || code == 82;
}
}  // namespace

std::string weatherCodeText(int code) {
  switch (code) {
    case 0:
      return "Clear";
    case 1:
    case 2:
    case 3:
      return "Cloudy";
    case 45:
    case 48:
      return "Fog";
    case 51:
    case 53:
    case 55:
      return "Drizzle";
    case 61:
    case 63:
    case 65:
    case 80:
    case 81:
    case 82:
      return "Rain";
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86:
      return "Snow";
    default:
      return code >= 0 ? "Code " + std::to_string(code) : "Unknown";
  }
}

std::string hourLabelFromIso(const std::string& iso) {
  if (iso.size() < 16) {
    return "--:--";
  }
  return iso.substr(11, 5);
}

std::string formatTempRange(float minTemp, float maxTemp) {
  if (std::isnan(minTemp) || std::isnan(maxTemp)) {
    return "Temp range unavailable";
  }
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "Range %.1f..%.1f C", minTemp, maxTemp);
  return buffer;
}

bool isRainyWeatherCode(int code) {
  return isRainyWeatherCodeImpl(code);
}

int weatherCodeSeverity(int code) {
  if (code >= 95) return 100;
  if (code >= 80 && code <= 82) return 90;
  if (code == 65) return 85;
  if (code == 63) return 80;
  if (code == 61) return 75;
  if (code == 55) return 70;
  if (code == 53) return 65;
  if (code == 51) return 60;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return 55;
  if (code == 45 || code == 48) return 30;
  if (code == 3) return 20;
  if (code == 2) return 15;
  if (code == 1) return 10;
  if (code == 0) return 5;
  return -1;
}

std::vector<ThreeHourBucket> buildThreeHourBuckets(const std::vector<HourlyForecast>& hours,
                                                  const std::string& currentHourLabel,
                                                  size_t bucketCount) {
  std::vector<ThreeHourBucket> buckets;
  if (bucketCount == 0) {
    return buckets;
  }

  size_t startIdx = hours.size();
  for (size_t i = 0; i < hours.size(); ++i) {
    if (hours[i].time.compare(0, currentHourLabel.size(), currentHourLabel) >= 0) {
      startIdx = i;
      break;
    }
  }
  if (startIdx >= hours.size()) {
    return buckets;
  }

  buckets.reserve(bucketCount);
  for (size_t b = 0; b < bucketCount; ++b) {
    const size_t base = startIdx + b * 3;
    if (base >= hours.size()) {
      break;
    }

    ThreeHourBucket bucket;
    const HourlyForecast& head = hours[base];
    bucket.start_label = head.time.size() >= 13 ? head.time.substr(11, 2) : "--";
    bucket.start_hour = bucket.start_label == "--" ? -1 : std::atoi(bucket.start_label.c_str());
    bucket.start_temperature = head.temperature;
    bucket.max_precipitation_probability = head.precipitation_probability;
    bucket.worst_weather_code = head.weather_code;
    int worstSeverity = weatherCodeSeverity(head.weather_code);

    for (size_t k = 1; k < 3; ++k) {
      const size_t idx = base + k;
      if (idx >= hours.size()) {
        break;
      }
      const HourlyForecast& h = hours[idx];
      bucket.max_precipitation_probability =
          std::max(bucket.max_precipitation_probability, h.precipitation_probability);
      const int severity = weatherCodeSeverity(h.weather_code);
      if (severity > worstSeverity) {
        worstSeverity = severity;
        bucket.worst_weather_code = h.weather_code;
      }
    }

    buckets.push_back(bucket);
  }

  return buckets;
}

RemainingForecastSummary summarizeRemainingHours(const std::vector<HourlyForecast>& hours,
                                                const std::string& currentHourLabel,
                                                int rainThreshold) {
  RemainingForecastSummary summary;

  for (const auto& hour : hours) {
    if (hour.time.compare(0, currentHourLabel.size(), currentHourLabel) < 0) {
      continue;
    }

    if (!summary.has_data) {
      summary.min_temperature = hour.temperature;
      summary.max_temperature = hour.temperature;
      summary.max_rain_chance = hour.precipitation_probability;
      summary.has_data = true;
    } else {
      if (!std::isnan(hour.temperature)) {
        if (std::isnan(summary.min_temperature) || hour.temperature < summary.min_temperature) {
          summary.min_temperature = hour.temperature;
        }
        if (std::isnan(summary.max_temperature) || hour.temperature > summary.max_temperature) {
          summary.max_temperature = hour.temperature;
        }
      }
      summary.max_rain_chance = std::max(summary.max_rain_chance, hour.precipitation_probability);
    }

    const bool rainy = hour.precipitation_probability >= rainThreshold || isRainyWeatherCodeImpl(hour.weather_code);
    if (summary.next_rain_at.empty() && rainy) {
      summary.next_rain_at = hourLabelFromIso(hour.time);
    }
  }

  return summary;
}

}  // namespace dooard
