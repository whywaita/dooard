#include <unity.h>

#include <cstdint>
#include <vector>

#include "power_policy.h"
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
  TEST_ASSERT_EQUAL_STRING(
      "19:30", dooard::hourLabelFromIso("2026-04-28T19:30").c_str());
}

void test_summarize_remaining_hours_uses_remaining_hours_only() {
  const std::vector<HourlyForecast> hours = {
      {"2026-04-28T18:00", 9.0f, 10, 0},
      {"2026-04-28T19:00", 10.0f, 20, 2},
      {"2026-04-28T20:00", 11.0f, 60, 61},
      {"2026-04-28T21:00", 12.0f, 30, 1},
  };

  const RemainingForecastSummary summary =
      dooard::summarizeRemainingHours(hours, "2026-04-28T19", 50);

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

  const RemainingForecastSummary summary =
      dooard::summarizeRemainingHours(hours, "2026-04-28T19", 50);

  TEST_ASSERT_TRUE(summary.has_data);
  TEST_ASSERT_EQUAL_STRING("", summary.next_rain_at.c_str());
  TEST_ASSERT_EQUAL_INT(30, summary.max_rain_chance);
}

void test_build_three_hour_buckets_aggregates_max_and_worst() {
  const std::vector<HourlyForecast> hours = {
      {"2026-04-28T13:00", 18.0f, 10, 0}, {"2026-04-28T14:00", 19.0f, 20, 1},
      {"2026-04-28T15:00", 20.0f, 50, 2}, {"2026-04-28T16:00", 21.0f, 80, 61},
      {"2026-04-28T17:00", 22.0f, 30, 0}, {"2026-04-28T18:00", 21.0f, 20, 1},
      {"2026-04-28T19:00", 19.0f, 10, 1},
  };

  const auto buckets = dooard::buildThreeHourBuckets(hours, "2026-04-28T14", 2);

  TEST_ASSERT_EQUAL_size_t(2, buckets.size());
  TEST_ASSERT_EQUAL_STRING("14", buckets[0].start_label.c_str());
  TEST_ASSERT_EQUAL_INT(14, buckets[0].start_hour);
  TEST_ASSERT_EQUAL_FLOAT(19.0f, buckets[0].start_temperature);
  TEST_ASSERT_EQUAL_INT(80, buckets[0].max_precipitation_probability);
  TEST_ASSERT_EQUAL_INT(61, buckets[0].worst_weather_code);

  TEST_ASSERT_EQUAL_STRING("17", buckets[1].start_label.c_str());
  TEST_ASSERT_EQUAL_INT(30, buckets[1].max_precipitation_probability);
  TEST_ASSERT_EQUAL_INT(1, buckets[1].worst_weather_code);
}

void test_build_three_hour_buckets_returns_partial_when_not_enough_hours() {
  const std::vector<HourlyForecast> hours = {
      {"2026-04-28T14:00", 19.0f, 20, 1},
      {"2026-04-28T15:00", 20.0f, 50, 2},
  };

  const auto buckets = dooard::buildThreeHourBuckets(hours, "2026-04-28T14", 6);

  TEST_ASSERT_EQUAL_size_t(1, buckets.size());
  TEST_ASSERT_EQUAL_STRING("14", buckets[0].start_label.c_str());
  TEST_ASSERT_EQUAL_INT(50, buckets[0].max_precipitation_probability);
  TEST_ASSERT_EQUAL_INT(2, buckets[0].worst_weather_code);
}

void test_build_three_hour_buckets_skips_past_hours() {
  const std::vector<HourlyForecast> hours = {
      {"2026-04-28T10:00", 12.0f, 90, 65}, {"2026-04-28T11:00", 13.0f, 80, 61},
      {"2026-04-28T14:00", 20.0f, 10, 0},  {"2026-04-28T15:00", 21.0f, 0, 0},
      {"2026-04-28T16:00", 22.0f, 0, 0},
  };

  const auto buckets = dooard::buildThreeHourBuckets(hours, "2026-04-28T14", 1);

  TEST_ASSERT_EQUAL_size_t(1, buckets.size());
  TEST_ASSERT_EQUAL_INT(10, buckets[0].max_precipitation_probability);
  TEST_ASSERT_EQUAL_INT(0, buckets[0].worst_weather_code);
}

void test_weather_code_severity_orders_rain_above_clear() {
  TEST_ASSERT_TRUE(dooard::weatherCodeSeverity(61) >
                   dooard::weatherCodeSeverity(0));
  TEST_ASSERT_TRUE(dooard::weatherCodeSeverity(95) >
                   dooard::weatherCodeSeverity(82));
  TEST_ASSERT_TRUE(dooard::weatherCodeSeverity(3) >
                   dooard::weatherCodeSeverity(1));
}

void test_weather_endpoint_uses_plain_http() {
  TEST_ASSERT_EQUAL_STRING("http://api.open-meteo.com/v1/forecast",
                           dooard::kWeatherApiBaseUrl);
}

void test_weather_refresh_interval_is_thirty_minutes() {
  TEST_ASSERT_EQUAL_UINT32(30UL * 60UL * 1000UL,
                           dooard::kWeatherRefreshIntervalMs);
  TEST_ASSERT_EQUAL_UINT64(30ULL * 60ULL * 1000ULL * 1000ULL,
                           dooard::weatherRefreshIntervalUs());
}

void test_display_and_cpu_power_policy_values() {
  TEST_ASSERT_EQUAL_UINT8(64, dooard::kDisplayBrightness);
  TEST_ASSERT_EQUAL_UINT32(240, dooard::kActiveCpuFrequencyMhz);
  TEST_ASSERT_EQUAL_UINT32(80, dooard::kIdleCpuFrequencyMhz);
}

void test_core2_button_wakeup_gpio_mask() {
  TEST_ASSERT_EQUAL_UINT8(37, dooard::kButtonAGpio);
  TEST_ASSERT_EQUAL_UINT8(38, dooard::kButtonBGpio);
  TEST_ASSERT_EQUAL_UINT8(39, dooard::kButtonCGpio);
  TEST_ASSERT_EQUAL_UINT64((1ULL << 37) | (1ULL << 38) | (1ULL << 39),
                           dooard::buttonWakeupMask());
}

void test_button_refresh_labels_match_requirement() {
  TEST_ASSERT_EQUAL_STRING("Refreshing", dooard::kButtonWakeTitle);
  TEST_ASSERT_EQUAL_STRING("Button pressed", dooard::kButtonWakeReason);
}

void setUp(void) {}

void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_weather_code_text_maps_expected_codes);
  RUN_TEST(test_hour_label_from_iso_extracts_hour);
  RUN_TEST(test_summarize_remaining_hours_uses_remaining_hours_only);
  RUN_TEST(test_summarize_remaining_hours_handles_no_rain_expected);
  RUN_TEST(test_build_three_hour_buckets_aggregates_max_and_worst);
  RUN_TEST(test_build_three_hour_buckets_returns_partial_when_not_enough_hours);
  RUN_TEST(test_build_three_hour_buckets_skips_past_hours);
  RUN_TEST(test_weather_code_severity_orders_rain_above_clear);
  RUN_TEST(test_weather_endpoint_uses_plain_http);
  RUN_TEST(test_weather_refresh_interval_is_thirty_minutes);
  RUN_TEST(test_display_and_cpu_power_policy_values);
  RUN_TEST(test_core2_button_wakeup_gpio_mask);
  RUN_TEST(test_button_refresh_labels_match_requirement);
  return UNITY_END();
}
