#pragma once

#include <string>

#include "ota_check.h"

namespace dooard {
namespace ota {

#if defined(ARDUINO)
bool performOtaUpdate(const OtaManifest &manifest, std::string &error);
#endif

} // namespace ota
} // namespace dooard
