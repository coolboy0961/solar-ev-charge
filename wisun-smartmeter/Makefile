PIO ?= $(shell command -v pio 2>/dev/null || echo /tmp/pio_env/bin/pio)

.PHONY: test build flash monitor monitor-log clean all

## Run unit tests (native, no hardware needed)
test:
	$(PIO) test -e native

## Build firmware for M5StickC Plus
build:
	$(PIO) run -e m5stick-c-plus

## Flash firmware to connected device
flash:
	$(PIO) run -e m5stick-c-plus -t upload

## Serial monitor (115200 baud)
monitor:
	$(PIO) device monitor -b 115200

## Serial monitor with timestamp, logged to file
monitor-log:
	$(PIO) device monitor -b 115200 -f time 2>&1 | tee /tmp/wisun-monitor.log

## Flash + monitor
flash-monitor: flash monitor

## Clean build artifacts
clean:
	$(PIO) run -t clean

## Test + build (CI向け)
all: test build
