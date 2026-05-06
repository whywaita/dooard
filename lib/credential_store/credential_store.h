#pragma once

#include <array>
#include <cstdint>
#include <string>

#if defined(ARDUINO_ARCH_ESP32) && __has_include(<Preferences.h>)
#include <Preferences.h>
#define DOOARD_CREDENTIALS_HAS_PREFERENCES 1
#else
#define DOOARD_CREDENTIALS_HAS_PREFERENCES 0
#endif

namespace dooard {
namespace credentials {

constexpr const char *kNamespace = "dooard-creds";
constexpr uint32_t kCurrentSchemaVersion = 1;

enum class CredentialKey : uint8_t {
  kWifiSsid,
  kWifiPassword,
  kApiKey,
  kDeviceId,
  kEndpointUrl,
  kConfigured,
  kSchemaVersion,
  kChecksum,
};

enum class CredentialAccessMode : uint8_t {
  kReadOnly,
  kReadWrite,
};

enum class CredentialStatus : uint8_t {
  kOk,
  kNotConfigured,
  kMissingRequiredValue,
  kUnsupportedVersion,
  kChecksumMismatch,
  kStorageUnavailable,
};

struct CredentialRecord {
  std::string wifi_ssid;
  std::string wifi_password;
  std::string api_key;
  std::string device_id;
  std::string endpoint_url;
  bool configured = false;
  uint32_t schema_version = 0;
  uint32_t checksum = 0;
};

const std::array<CredentialKey, 8> &allCredentialKeys();
const char *keyName(CredentialKey key);
const char *credentialStatusText(CredentialStatus status);
uint32_t calculateCredentialChecksum(const CredentialRecord &record);
void finalizeCredentialRecord(CredentialRecord &record);
CredentialStatus validateCredentialRecord(const CredentialRecord &record);

class CredentialStore {
public:
  bool begin(CredentialAccessMode mode);
  void end();
  bool load(CredentialRecord &out);
  bool save(CredentialRecord record);
  bool clear();
  bool isOpen() const;
  CredentialStatus lastStatus() const;

private:
#if DOOARD_CREDENTIALS_HAS_PREFERENCES
  Preferences preferences_;
#endif
  bool open_ = false;
  CredentialAccessMode mode_ = CredentialAccessMode::kReadOnly;
  CredentialStatus last_status_ = CredentialStatus::kStorageUnavailable;
};

} // namespace credentials
} // namespace dooard
