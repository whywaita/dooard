UV ?= ~/.local/bin/uv

.PHONY: sync test lint build monitor

sync:
	$(UV) sync

test:
	$(UV) run pytest

lint:
	$(UV) run pio check

build:
	$(UV) run pio run

monitor:
	$(UV) run pio device monitor
