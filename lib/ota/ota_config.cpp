#include "ota_config.h"

#include <cctype>

namespace dooard {
namespace ota {
namespace {

bool parsePart(const char *text, size_t &index, uint32_t &part) {
  if (text[index] == '\0' ||
      !std::isdigit(static_cast<unsigned char>(text[index]))) {
    return false;
  }

  uint32_t value = 0;
  while (std::isdigit(static_cast<unsigned char>(text[index]))) {
    value = value * 10U + static_cast<uint32_t>(text[index] - '0');
    ++index;
  }
  part = value;
  return true;
}

} // namespace

const char *currentFirmwareVersion() { return kFirmwareVersion; }

bool parseVersion(const char *text, Version &out) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }

  size_t index = 0;
  if (text[index] == 'v' || text[index] == 'V') {
    ++index;
  }

  Version parsed;
  if (!parsePart(text, index, parsed.major)) {
    return false;
  }

  if (text[index] == '\0') {
    out = parsed;
    return true;
  }
  if (text[index] != '.') {
    return false;
  }
  ++index;

  if (!parsePart(text, index, parsed.minor)) {
    return false;
  }

  if (text[index] == '\0') {
    out = parsed;
    return true;
  }
  if (text[index] != '.') {
    return false;
  }
  ++index;

  if (!parsePart(text, index, parsed.patch)) {
    return false;
  }
  if (text[index] != '\0') {
    return false;
  }

  out = parsed;
  return true;
}

int compareVersions(const Version &left, const Version &right) {
  if (left.major != right.major) {
    return left.major < right.major ? -1 : 1;
  }
  if (left.minor != right.minor) {
    return left.minor < right.minor ? -1 : 1;
  }
  if (left.patch != right.patch) {
    return left.patch < right.patch ? -1 : 1;
  }
  return 0;
}

int compareVersions(const char *left, const char *right) {
  Version parsedLeft;
  Version parsedRight;
  if (!parseVersion(left, parsedLeft) || !parseVersion(right, parsedRight)) {
    return 0;
  }
  return compareVersions(parsedLeft, parsedRight);
}

bool isNewerVersion(const char *current, const char *candidate) {
  Version currentVersion;
  Version candidateVersion;
  if (!parseVersion(current, currentVersion) ||
      !parseVersion(candidate, candidateVersion)) {
    return false;
  }
  return compareVersions(currentVersion, candidateVersion) < 0;
}

bool isSha256Hex(const std::string &value) {
  if (value.length() != 64U) {
    return false;
  }

  for (const char ch : value) {
    if (!std::isxdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  return true;
}

} // namespace ota
} // namespace dooard
