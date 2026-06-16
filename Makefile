# Operator entrypoints. Run `make` (or `make help`) to list targets.
# The hub virtualenv lives in hub/.venv (created by `make venv`).

PYTHON ?= python3

.DEFAULT_GOAL := help
.PHONY: help venv hub test-hub check-session firmware-venv firmware-build firmware-upload firmware-monitor

help:
	@echo "make venv                         create hub/.venv and install deps (incl. tests)"
	@echo "make hub                          run the hub on 0.0.0.0:5000"
	@echo "make test-hub                     run the hub test suite"
	@echo "make check-session SESSION=<id> [ARGS=...]   validate a session (PASS/FAIL)"
	@echo "      e.g. ARGS=\"--min-frames 100 --max-gaps 0 --min-rssi -70\""
	@echo "make firmware-venv                create firmware/.venv with PlatformIO (pip)"
	@echo "make firmware-build               pio run            (run firmware-venv first)"
	@echo "make firmware-upload              pio run -t upload  (flash the board)"
	@echo "make firmware-monitor             pio device monitor (serial @ 115200)"

venv:
	cd hub && $(PYTHON) -m venv .venv \
		&& .venv/bin/pip install -q -U pip \
		&& .venv/bin/pip install -q -r requirements-dev.txt

hub:
	cd hub && .venv/bin/python run.py

test-hub:
	cd hub && .venv/bin/python -m pytest -q

check-session:
	@test -n "$(SESSION)" || { echo "usage: make check-session SESSION=<session_id> [ARGS=\"--min-frames 100 ...\"]"; exit 2; }
	cd hub && .venv/bin/python -m pallet_hub.check_session "$(SESSION)" $(ARGS)

# PlatformIO lives in firmware/.venv (the system `pio` from apt is too old and
# breaks with modern Click). Run `make firmware-venv` once first.
firmware-venv:
	cd firmware && $(PYTHON) -m venv .venv \
		&& .venv/bin/pip install -q -U pip \
		&& .venv/bin/pip install -q -U platformio
	@firmware/.venv/bin/pio --version

firmware-build:
	@test -x firmware/.venv/bin/pio || { echo "run 'make firmware-venv' first"; exit 2; }
	cd firmware && .venv/bin/pio run

firmware-upload:
	@test -x firmware/.venv/bin/pio || { echo "run 'make firmware-venv' first"; exit 2; }
	cd firmware && .venv/bin/pio run -t upload

firmware-monitor:
	@test -x firmware/.venv/bin/pio || { echo "run 'make firmware-venv' first"; exit 2; }
	cd firmware && .venv/bin/pio device monitor
