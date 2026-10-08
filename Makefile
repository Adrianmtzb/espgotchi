# ESPgotchi — development shortcuts.
# Plain `make` lists the targets.

FQBN    := esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=no_ota,FlashSize=4M
SKETCH  := firmware/espgotchi
BUILD   := $(SKETCH)/build/esp32.esp32.esp32c6
CORE    := esp32:esp32@3.2.0

# Pinned versions: a library update must never break the build without anyone
# touching the code. CI installs exactly these.
LIBS := "GFX Library for Arduino@1.6.4" "ArduinoJson@7.4.2"

# The C6 enumerates as native USB (usbmodem / ttyACM). Override with PORT=...
PORT ?= $(shell ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null | head -1)
BAUD ?= 115200

# Where arduino-cli keeps the cores (boot_app0.bin lives there).
DATA_DIR = $(shell arduino-cli config get directories.data 2>/dev/null || echo $$HOME/.arduino15)

.DEFAULT_GOAL := help
.PHONY: help deps gen sprites shot build bootapp0 flash monitor check site serve clean bump

help: ## Show this help
	@echo
	@echo "ESPgotchi"
	@echo
	@grep -E '^[a-zA-Z0-9_-]+:.*?## .*$$' $(MAKEFILE_LIST) \
		| awk 'BEGIN {FS = ":.*?## "} {printf "  \033[36m%-10s\033[0m %s\n", $$1, $$2}'
	@echo
	@echo "  Detected port: $(if $(PORT),$(PORT),none)"
	@echo

deps: ## Install the ESP32 core and libraries (CI uses this too)
	arduino-cli core update-index
	arduino-cli core install $(CORE)
	arduino-cli lib install $(LIBS)

gen: ## Regenerate sprites.h and web_assets.h from shared/ and web/
	@node tools/gen_sprites.mjs
	@node tools/gen_web.mjs

sprites: ## Render a PNG contact sheet of every sprite (needs macOS sips)
	@python3 tools/preview_sprites.py /tmp/espgotchi-sprites.ppm

shot: ## Capture the board's screen to screenshot.png over serial (OUT=path to change)
	@test -n "$(PORT)" || { echo "No serial port found."; exit 1; }
	@python3 tools/screenshot.py $(PORT) $(or $(OUT),screenshot.png)

build: gen ## Regenerate assets and compile the firmware
	arduino-cli compile --fqbn "$(FQBN)" --export-binaries --warnings default $(SKETCH)

bootapp0: ## Copy boot_app0.bin from the installed core next to the binaries
	@src=$$(find "$(DATA_DIR)/packages/esp32/hardware/esp32" -name boot_app0.bin 2>/dev/null | head -1); \
	 test -n "$$src" || { echo "boot_app0.bin not found in the core. Run: make deps"; exit 1; }; \
	 cp "$$src" "$(BUILD)/"

flash: build ## Compile and upload to the board
	@test -n "$(PORT)" || { \
		echo "No serial port found."; \
		echo "Plug the board in, or pass it: make flash PORT=/dev/cu.usbmodemXXXX"; \
		exit 1; }
	@echo "Uploading to $(PORT)"
	@arduino-cli upload --fqbn "$(FQBN)" -p $(PORT) $(SKETCH) || { \
		echo; \
		echo "If it failed to connect: hold BOOT, tap RST, release BOOT and retry."; \
		echo "If the port is busy, close any open serial monitor (lsof $(PORT))."; \
		exit 1; }

monitor: ## Open the serial console (type 'help' inside; Ctrl-C to leave)
	@test -n "$(PORT)" || { echo "No serial port found."; exit 1; }
	arduino-cli monitor -p $(PORT) --config baudrate=$(BAUD)

check: ## Validate the installer manifest, the version and the generated assets
	@python3 scripts/check-manifest.py
	@node tools/gen_sprites.mjs --check
	@node tools/gen_web.mjs --check

site: build bootapp0 ## Assemble the web installer in _site/, same as CI
	@rm -rf _site && mkdir -p _site
	@cp docs/index.html docs/manifest.json _site/
	@cp shared/sprites.json _site/sprites.json
	@cp $(BUILD)/espgotchi.ino.bin            _site/espgotchi.bin
	@cp $(BUILD)/espgotchi.ino.bootloader.bin _site/bootloader.bin
	@cp $(BUILD)/espgotchi.ino.partitions.bin _site/partitions.bin
	@cp $(BUILD)/boot_app0.bin                _site/boot_app0.bin
	@echo "Installer assembled in _site/"

serve: site ## Serve the installer locally (Web Serial works on localhost)
	@echo
	@echo "Open http://localhost:8000 in Chrome or Edge."
	@echo
	@cd _site && python3 -m http.server 8000

clean: ## Remove build artifacts and the local site
	rm -rf $(SKETCH)/build _site

bump: ## Set the version in both places: make bump VERSION=0.2.0
	@test -n "$(VERSION)" || { echo "Missing version: make bump VERSION=0.2.0"; exit 1; }
	@echo "$(VERSION)" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$$' \
		|| { echo "Version must be X.Y.Z"; exit 1; }
	@sed -i.bak -E 's/(#define[[:space:]]+FW_VERSION[[:space:]]+")[^"]+(")/\1$(VERSION)\2/' \
		$(SKETCH)/config.h && rm -f $(SKETCH)/config.h.bak
	@sed -i.bak -E 's/("version"[[:space:]]*:[[:space:]]*")[^"]+(")/\1$(VERSION)\2/' \
		docs/manifest.json && rm -f docs/manifest.json.bak
	@$(MAKE) --no-print-directory check
	@echo
	@echo "Version $(VERSION) set in config.h and manifest.json."
	@echo "Once tested:"
	@echo "  git commit -am 'Version $(VERSION)' && git tag v$(VERSION) && git push --follow-tags"
