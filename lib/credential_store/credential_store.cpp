#include "credential_store.h"

#include <cstddef>

#if DOOARD_CREDENTIALS_HAS_PREFERENCES
#include <WString.h>
#endif

namespace dooard {
namespace credentials {
namespace {

constexpr uint32_t kFnvOffsetBasis = 2166136261UL;
constexpr uint32_t kFnvPrime = 16777619UL;

void updateChecksumByte(uint32_t &checksum, uint8_t value) {
  checksum ^= value;
  checksum *= kFnvPrime;
}

void updateChecksumUint32(uint32_t &checksum, uint32_t value) {
  for (uint8_t index = 0; index < 4; ++index) {
    updateChecksumByte(checksum,
                       static_cast<uint8_t>((value >> (index * 8)) & 0xffU));
  }
}

void updateChecksumString(uint32_t &checksum, const std::string &value) {
  updateChecksumUint32(checksum, static_cast<uint32_t>(value.size()));
  for (const char ch : value) {
    updateChecksumByte(checksum, static_cast<uint8_t>(ch));
  }
}

bool hasRequiredValues(const CredentialRecord &record) {
  return !record.wifi_ssid.empty() && !record.wifi_password.empty() &&
         !record.device_id.empty() && !record.endpoint_url.empty();
}

#if DOOARD_CREDENTIALS_HAS_PREFERENCES
std::string getString(Preferences &preferences, CredentialKey key) {
  const String value = preferences.getString(keyName(key), "");
  return std::string(value.c_str());
}

bool putRequiredString(Preferences &preferences, CredentialKey key,
                       const std::string &value) {
  return preferences.putString(keyName(key), value.c_str()) > 0;
}
#endif

} // namespace

const std::array<CredentialKey, 8> &allCredentialKeys() {
  static const std::array<CredentialKey, 8> keys = {{
      CredentialKey::kWifiSsid,
      CredentialKey::kWifiPassword,
      CredentialKey::kApiKey,
      CredentialKey::kDeviceId,
      CredentialKey::kEndpointUrl,
      CredentialKey::kConfigured,
      CredentialKey::kSchemaVersion,
      CredentialKey::kChecksum,
  }};
  return keys;
}

const char *keyName(CredentialKey key) {
  switch (key) {
  case CredentialKey::kWifiSsid:
    return "wifi_ssid";
  case CredentialKey::kWifiPassword:
    return "wifi_password";
  case CredentialKey::kApiKey:
    return "api_key";
  case CredentialKey::kDeviceId:
    return "device_id";
  case CredentialKey::kEndpointUrl:
    return "endpoint_url";
  case CredentialKey::kConfigured:
    return "configured";
  case CredentialKey::kSchemaVersion:
    return "schema_version";
  case CredentialKey::kChecksum:
    return "checksum";
  }
  return "";
}

const char *credentialStatusText(CredentialStatus status) {
  switch (status) {
  case CredentialStatus::kOk:
    return "ok";
  case CredentialStatus::kNotConfigured:
    return "not configured";
  case CredentialStatus::kMissingRequiredValue:
    return "missing required value";
  case CredentialStatus::kUnsupportedVersion:
    return "unsupported version";
  case CredentialStatus::kChecksumMismatch:
    return "checksum mismatch";
  case CredentialStatus::kStorageUnavailable:
    return "storage unavailable";
  }
  return "unknown";
}

uint32_t calculateCredentialChecksum(const CredentialRecord &record) {
  uint32_t checksum = kFnvOffsetBasis;
  updateChecksumUint32(checksum, record.schema_version);
  updateChecksumByte(checksum, record.configured ? 1U : 0U);
  updateChecksumString(checksum, record.wifi_ssid);
  updateChecksumString(checksum, record.wifi_password);
  updateChecksumString(checksum, record.api_key);
  updateChecksumString(checksum, record.device_id);
  updateChecksumString(checksum, record.endpoint_url);
  return checksum;
}

void finalizeCredentialRecord(CredentialRecord &record) {
  record.configured = true;
  record.schema_version = kCurrentSchemaVersion;
  record.checksum = calculateCredentialChecksum(record);
}

CredentialStatus validateCredentialRecord(const CredentialRecord &record) {
  if (!record.configured) {
    return CredentialStatus::kNotConfigured;
  }
  if (record.schema_version != kCurrentSchemaVersion) {
    return CredentialStatus::kUnsupportedVersion;
  }
  if (!hasRequiredValues(record)) {
    return CredentialStatus::kMissingRequiredValue;
  }
  if (record.checksum != calculateCredentialChecksum(record)) {
    return CredentialStatus::kChecksumMismatch;
  }
  return CredentialStatus::kOk;
}

bool CredentialStore::begin(CredentialAccessMode mode) {
  mode_ = mode;
#if DOOARD_CREDENTIALS_HAS_PREFERENCES
  open_ =
      preferences_.begin(kNamespace, mode == CredentialAccessMode::kReadOnly);
  last_status_ =
      open_ ? CredentialStatus::kOk : CredentialStatus::kStorageUnavailable;
  return open_;
#else
  open_ = false;
  last_status_ = CredentialStatus::kStorageUnavailable;
  return false;
#endif
}

void CredentialStore::end() {
#if DOOARD_CREDENTIALS_HAS_PREFERENCES
  if (open_) {
    preferences_.end();
  }
#endif
  open_ = false;
}

bool CredentialStore::load(CredentialRecord &out) {
  if (!open_) {
    last_status_ = CredentialStatus::kStorageUnavailable;
    return false;
  }

#if DOOARD_CREDENTIALS_HAS_PREFERENCES
  CredentialRecord record;
  record.wifi_ssid = getString(preferences_, CredentialKey::kWifiSsid);
  record.wifi_password = getString(preferences_, CredentialKey::kWifiPassword);
  record.api_key = getString(preferences_, CredentialKey::kApiKey);
  record.device_id = getString(preferences_, CredentialKey::kDeviceId);
  record.endpoint_url = getString(preferences_, CredentialKey::kEndpointUrl);
  record.configured =
      preferences_.getBool(keyName(CredentialKey::kConfigured), false);
  record.schema_version =
      preferences_.getUInt(keyName(CredentialKey::kSchemaVersion), 0);
  record.checksum = preferences_.getUInt(keyName(CredentialKey::kChecksum), 0);
  out = record;
  last_status_ = validateCredentialRecord(out);
  return last_status_ == CredentialStatus::kOk;
#else
  (void)out;
  last_status_ = CredentialStatus::kStorageUnavailable;
  return false;
#endif
}

bool CredentialStore::save(CredentialRecord record) {
  if (!open_ || mode_ != CredentialAccessMode::kReadWrite) {
    last_status_ = CredentialStatus::kStorageUnavailable;
    return false;
  }

  finalizeCredentialRecord(record);
  last_status_ = validateCredentialRecord(record);
  if (last_status_ != CredentialStatus::kOk) {
    return false;
  }

#if DOOARD_CREDENTIALS_HAS_PREFERENCES
  if (!putRequiredString(preferences_, CredentialKey::kWifiSsid,
                         record.wifi_ssid) ||
      !putRequiredString(preferences_, CredentialKey::kWifiPassword,
                         record.wifi_password) ||
      !putRequiredString(preferences_, CredentialKey::kDeviceId,
                         record.device_id) ||
      !putRequiredString(preferences_, CredentialKey::kEndpointUrl,
                         record.endpoint_url)) {
    last_status_ = CredentialStatus::kStorageUnavailable;
    return false;
  }

  preferences_.putString(keyName(CredentialKey::kApiKey),
                         record.api_key.c_str());
  preferences_.putBool(keyName(CredentialKey::kConfigured), record.configured);
  preferences_.putUInt(keyName(CredentialKey::kSchemaVersion),
                       record.schema_version);
  preferences_.putUInt(keyName(CredentialKey::kChecksum), record.checksum);
  last_status_ = CredentialStatus::kOk;
  return true;
#else
  last_status_ = CredentialStatus::kStorageUnavailable;
  return false;
#endif
}

bool CredentialStore::clear() {
  if (!open_ || mode_ != CredentialAccessMode::kReadWrite) {
    last_status_ = CredentialStatus::kStorageUnavailable;
    return false;
  }

#if DOOARD_CREDENTIALS_HAS_PREFERENCES
  const bool cleared = preferences_.clear();
  last_status_ = cleared ? CredentialStatus::kNotConfigured
                         : CredentialStatus::kStorageUnavailable;
  return cleared;
#else
  last_status_ = CredentialStatus::kStorageUnavailable;
  return false;
#endif
}

bool CredentialStore::isOpen() const { return open_; }

CredentialStatus CredentialStore::lastStatus() const { return last_status_; }

} // namespace credentials
} // namespace dooard
