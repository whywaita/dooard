#!/usr/bin/env bash

set -euo pipefail

cd "$(dirname "$0")/.."

grep -Eq '^burn:$' Makefile
grep -F '$(UV) run pio run -t upload $(if $(UPLOAD_PORT),--upload-port $(UPLOAD_PORT),)' Makefile >/dev/null
grep -F 'upload_port = auto' platformio.ini >/dev/null
