UV ?= ~/.local/bin/uv

.PHONY: sync build monitor

sync:
	$(UV) sync

build:
	$(UV) run pio run

monitor:
	$(UV) run pio device monitor
