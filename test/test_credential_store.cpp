#include <unity.h>

#include <cstring>

#include "credential_store.h"

using dooard::credentials::CredentialKey;
using dooard::credentials::CredentialRecord;
using dooard::credentials::CredentialStatus;

void test_credential_store_namespace_and_key_names_are_nvs_safe() {
  TEST_ASSERT_EQUAL_STRING("dooard-creds", dooard::credentials::kNamespace);

  for (const CredentialKey key : dooard::credentials::allCredentialKeys()) {
    TEST_ASSERT_LESS_OR_EQUAL_size_t(
        15, std::strlen(dooard::credentials::keyName(key)));
  }

  TEST_ASSERT_EQUAL_STRING(
      "wifi_ssid", dooard::credentials::keyName(CredentialKey::kWifiSsid));
  TEST_ASSERT_EQUAL_STRING("wifi_password", dooard::credentials::keyName(
                                                CredentialKey::kWifiPassword));
  TEST_ASSERT_EQUAL_STRING(
      "api_key", dooard::credentials::keyName(CredentialKey::kApiKey));
  TEST_ASSERT_EQUAL_STRING(
      "device_id", dooard::credentials::keyName(CredentialKey::kDeviceId));
  TEST_ASSERT_EQUAL_STRING("endpoint_url", dooard::credentials::keyName(
                                               CredentialKey::kEndpointUrl));
  TEST_ASSERT_EQUAL_STRING(
      "configured", dooard::credentials::keyName(CredentialKey::kConfigured));
  TEST_ASSERT_EQUAL_STRING(
      "schema_version",
      dooard::credentials::keyName(CredentialKey::kSchemaVersion));
  TEST_ASSERT_EQUAL_STRING(
      "checksum", dooard::credentials::keyName(CredentialKey::kChecksum));
}

void test_credential_record_finalize_sets_version_configured_and_checksum() {
  CredentialRecord record;
  record.wifi_ssid = "office-wifi";
  record.wifi_password = "correct horse battery staple";
  record.api_key = "api-secret";
  record.device_id = "core2-01";
  record.endpoint_url = "https://api.example.test/dooard";

  dooard::credentials::finalizeCredentialRecord(record);

  TEST_ASSERT_TRUE(record.configured);
  TEST_ASSERT_EQUAL_UINT32(dooard::credentials::kCurrentSchemaVersion,
                           record.schema_version);
  TEST_ASSERT_NOT_EQUAL_UINT32(0, record.checksum);
  TEST_ASSERT_EQUAL_UINT32(
      dooard::credentials::calculateCredentialChecksum(record),
      record.checksum);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(CredentialStatus::kOk),
      static_cast<uint8_t>(
          dooard::credentials::validateCredentialRecord(record)));
}

void test_credential_record_validation_detects_not_configured_missing_required_and_checksum() {
  CredentialRecord record;
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(CredentialStatus::kNotConfigured),
      static_cast<uint8_t>(
          dooard::credentials::validateCredentialRecord(record)));

  record.configured = true;
  record.schema_version = dooard::credentials::kCurrentSchemaVersion;
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(CredentialStatus::kMissingRequiredValue),
      static_cast<uint8_t>(
          dooard::credentials::validateCredentialRecord(record)));

  record.wifi_ssid = "office-wifi";
  record.wifi_password = "secret";
  record.device_id = "core2-01";
  record.endpoint_url = "https://api.example.test/dooard";
  dooard::credentials::finalizeCredentialRecord(record);
  record.endpoint_url = "https://tampered.example.test/dooard";

  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(CredentialStatus::kChecksumMismatch),
      static_cast<uint8_t>(
          dooard::credentials::validateCredentialRecord(record)));
}

void test_credential_record_validation_detects_unsupported_schema_version() {
  CredentialRecord record;
  record.wifi_ssid = "office-wifi";
  record.wifi_password = "secret";
  record.device_id = "core2-01";
  record.endpoint_url = "https://api.example.test/dooard";
  dooard::credentials::finalizeCredentialRecord(record);
  record.schema_version = dooard::credentials::kCurrentSchemaVersion + 1;
  record.checksum = dooard::credentials::calculateCredentialChecksum(record);

  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(CredentialStatus::kUnsupportedVersion),
      static_cast<uint8_t>(
          dooard::credentials::validateCredentialRecord(record)));
}
