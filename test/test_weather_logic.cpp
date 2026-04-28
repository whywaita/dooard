#include <unity.h>

#include <vector>

#include "weather_logic.h"

using dooard::HourlyForecast;
using dooard::RemainingForecastSummary;

void test_weather_code_text_maps_expected_codes() {
  TEST_ASSERT_EQUAL_STRING("Clear", dooard::weatherCodeText(0).c_str());
  TEST_ASSERT_EQUAL_STRING("Cloudy", dooard::weatherCodeText(2).c_str());
  TEST_ASSERT_EQUAL_STRING("Rain", dooard::weatherCodeText(61).c_str());
  TEST_ASSERT_EQUAL_STRING("Snow", dooard::weatherCodeText(85).c_str());
}

void test_hour_label_from_iso_extracts_hour() {
  TEST_ASSERT_EQUAL_STRING("19:30", dooard::hourLabelFromIso("2026-04-28T19:30").c_str());
}

void test_summarize_remaining_hours_uses_remaining_hours_only() {
  const std::vector<HourlyForecast> hours = {
      {"2026-04-28T18:00", 9.0f, 10, 0},
      {"2026-04-28T19:00", 10.0f, 20, 2},
      {"2026-04-28T20:00", 11.0f, 60, 61},
      {"2026-04-28T21:00", 12.0f, 30, 1},
  };

  const RemainingForecastSummary summary = dooard::summarizeRemainingHours(hours, "2026-04-28T19", 50);

  TEST_ASSERT_TRUE(summary.has_data);
  TEST_ASSERT_EQUAL_FLOAT(10.0f, summary.min_temperature);
  TEST_ASSERT_EQUAL_FLOAT(12.0f, summary.max_temperature);
  TEST_ASSERT_EQUAL_INT(60, summary.max_rain_chance);
  TEST_ASSERT_EQUAL_STRING("20:00", summary.next_rain_at.c_str());
}

void test_summarize_remaining_hours_handles_no_rain_expected() {
  const std::vector<HourlyForecast> hours = {
      {"2026-04-28T19:00", 13.0f, 20, 1},
      {"2026-04-28T20:00", 14.0f, 30, 2},
  };

  const RemainingForecastSummary summary = dooard::summarizeRemainingHours(hours, "2026-04-28T19", 50);

  TEST_ASSERT_TRUE(summary.has_data);
  TEST_ASSERT_EQUAL_STRING("", summary.next_rain_at.c_str());
  TEST_ASSERT_EQUAL_INT(30, summary.max_rain_chance);
}

void setUp(void) {}

void tearDown(void) {}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_weather_code_text_maps_expected_codes);
  RUN_TEST(test_hour_label_from_iso_extracts_hour);
  RUN_TEST(test_summarize_remaining_hours_uses_remaining_hours_only);
  RUN_TEST(test_summarize_remaining_hours_handles_no_rain_expected);
  return UNITY_END();
}
