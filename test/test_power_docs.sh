#!/usr/bin/env sh
set -eu

doc="docs/power.md"
readme="README.md"

fail() {
  printf '%s\n' "power docs: $1" >&2
  exit 1
}

require_file() {
  [ -f "$1" ] || fail "missing $1"
}

require_contains() {
  file="$1"
  text="$2"
  grep -Fq "$text" "$file" || fail "missing '$text' in $file"
}

require_file "$doc"
require_contains "$readme" "[Power notes for M5Stack Core2](docs/power.md)"

require_contains "$doc" "# Power notes for M5Stack Core2"
require_contains "$doc" "## Current dooard behavior"
require_contains "$doc" "15 minutes"
require_contains "$doc" "Wi-Fi"
require_contains "$doc" "M5.Display.setBrightness"
require_contains "$doc" "M5.Power.getBatteryCurrent()"
require_contains "$doc" "USB power meter"
require_contains "$doc" "M5.Power.timerSleep"
require_contains "$doc" "AXP192"
require_contains "$doc" "AXP2101"
require_contains "$doc" "500 mAh"
require_contains "$doc" "390 mAh"
