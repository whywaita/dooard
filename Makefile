UV ?= uv
PIO ?= .venv/bin/pio
UPLOAD_PORT ?=

.PHONY: sync test test-docs test-native lint build burn monitor

sync:
	$(UV) sync

test: test-docs test-native

test-docs:
	sh test/test_power_docs.sh

test-native:
	$(PIO) test -e native

lint:
	$(PIO) check

build:
	$(PIO) run

burn:
	$(PIO) run -t upload $(if $(UPLOAD_PORT),--upload-port $(UPLOAD_PORT),)

monitor:
	$(PIO) device monitor
