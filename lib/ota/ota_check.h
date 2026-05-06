#pragma once

#include <cstddef>
#include <string>

namespace dooard {
namespace ota {

struct OtaManifest {
  std::string version;
  std::string firmware_url;
  std::string sha256;
  size_t size_bytes = 0;
};

bool parseManifestJson(const char *json, OtaManifest &out, std::string &error);

#if defined(ARDUINO)
bool fetchOtaManifest(OtaManifest &out, std::string &error);
#endif

} // namespace ota
} // namespace dooard
