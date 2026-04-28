#include "weather_logic.h"

#include <algorithm>
#include <cstdio>

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
