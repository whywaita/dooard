#pragma once

#include <cmath>
#include <string>
#include <vector>

namespace dooard {

struct HourlyForecast {
  std::string time;
  float temperature = NAN;
  int precipitation_probability = -1;
  int weather_code = -1;
};

struct RemainingForecastSummary {
  bool has_data = false;
  float min_temperature = NAN;
  float max_temperature = NAN;
  int max_rain_chance = -1;
  std::string next_rain_at;
};

std::string weatherCodeText(int code);
std::string hourLabelFromIso(const std::string& iso);
std::string formatTempRange(float minTemp, float maxTemp);
bool isRainyWeatherCode(int code);
RemainingForecastSummary summarizeRemainingHours(const std::vector<HourlyForecast>& hours,
                                                const std::string& currentHourLabel,
                                                int rainThreshold);

}  // namespace dooard
