UV ?= ~/.local/bin/uv
UPLOAD_PORT ?=

.PHONY: sync test lint build burn monitor

sync:
	$(UV) sync

test:
	$(UV) run pio test -e native

lint:
	$(UV) run pio check

build:
	$(UV) run pio run

burn:
	$(UV) run pio run -t upload $(if $(UPLOAD_PORT),--upload-port $(UPLOAD_PORT),)

monitor:
	$(UV) run pio device monitor
