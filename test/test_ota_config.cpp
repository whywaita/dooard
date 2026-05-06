#include <unity.h>

#include "ota_check.h"
#include "ota_config.h"

void test_ota_poll_interval_is_six_hours() {
  TEST_ASSERT_EQUAL_UINT32(6UL * 60UL * 60UL * 1000UL,
                           dooard::ota::kOtaPollIntervalMs);
}

void test_ota_version_compare_orders_semantic_versions() {
  TEST_ASSERT_TRUE(dooard::ota::isNewerVersion("1.2.3", "1.2.4"));
  TEST_ASSERT_TRUE(dooard::ota::isNewerVersion("1.2.3", "1.3.0"));
  TEST_ASSERT_TRUE(dooard::ota::isNewerVersion("1.2.3", "2.0.0"));
  TEST_ASSERT_FALSE(dooard::ota::isNewerVersion("1.2.3", "1.2.3"));
  TEST_ASSERT_FALSE(dooard::ota::isNewerVersion("1.2.3", "1.2.2"));
}

void test_ota_version_compare_accepts_v_prefix_and_missing_patch() {
  TEST_ASSERT_TRUE(dooard::ota::isNewerVersion("v1.2", "v1.2.1"));
  TEST_ASSERT_FALSE(dooard::ota::isNewerVersion("v1.2.0", "1.2"));
}

void test_ota_version_compare_rejects_invalid_versions() {
  TEST_ASSERT_FALSE(dooard::ota::isNewerVersion("1.2.3", "1.2.beta"));
  TEST_ASSERT_FALSE(dooard::ota::isNewerVersion("bad", "1.2.4"));
}

void test_parse_ota_manifest_requires_expected_fields() {
  dooard::ota::OtaManifest manifest;
  std::string error;

  TEST_ASSERT_TRUE(dooard::ota::parseManifestJson(
      "{\"version\":\"1.2.4\",\"firmware_url\":\"http://whywaita.github.io/"
      "dooard/firmware/firmware.bin\",\"sha256\":\"0123456789abcdef01234567"
      "89abcdef0123456789abcdef0123456789abcdef\",\"size_bytes\":1234}",
      manifest, error));

  TEST_ASSERT_EQUAL_STRING("1.2.4", manifest.version.c_str());
  TEST_ASSERT_EQUAL_STRING("http://whywaita.github.io/dooard/firmware/"
                           "firmware.bin",
                           manifest.firmware_url.c_str());
  TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef0123456789abcdef"
                           "0123456789abcdef",
                           manifest.sha256.c_str());
  TEST_ASSERT_EQUAL_size_t(1234, manifest.size_bytes);
}

void test_parse_ota_manifest_rejects_invalid_sha256() {
  dooard::ota::OtaManifest manifest;
  std::string error;

  TEST_ASSERT_FALSE(dooard::ota::parseManifestJson(
      "{\"version\":\"1.2.4\",\"firmware_url\":\"http://example.test/"
      "firmware.bin\",\"sha256\":\"short\"}",
      manifest, error));
}
