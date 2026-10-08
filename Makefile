# ESPgotchi — development shortcuts.
# Plain `make` lists the targets.

SKETCH  := firmware/espgotchi
CORE    := esp32:esp32@3.2.0

# Supported boards. The sketch picks its pinout from the compiler target, so a board is
# nothing more than an FQBN here: make build BOARD=s3
BOARD ?= c6
BOARDS := c6 s3
ifeq ($(BOARD),c6)
FQBN  := esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=no_ota,FlashSize=4M
BUILD := $(SKETCH)/build/esp32.esp32.esp32c6
else ifeq ($(BOARD),s3)
FQBN  := esp32:esp32:esp32s3:CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB,FlashSize=16M,PSRAM=opi
BUILD := $(SKETCH)/build/esp32.esp32.esp32s3
else
$(error Unknown BOARD '$(BOARD)'. Use one of: $(BOARDS))
endif

# Pinned versions: a library update must never break the build without anyone
# touching the code. CI installs exactly these.
LIBS := "GFX Library for Arduino@1.6.4" "ArduinoJson@7.4.2"

# Both boards enumerate as native USB (usbmodem / ttyACM). Override with PORT=...
PORT ?= $(shell ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null | head -1)
BAUD ?= 115200

# Where arduino-cli keeps the cores (boot_app0.bin lives there).
DATA_DIR = $(shell arduino-cli config get directories.data 2>/dev/null || echo $$HOME/.arduino15)

.DEFAULT_GOAL := help
.PHONY: help deps gen sprites shot build build-all bootapp0 flash monitor check site print-build serve clean bump

help: ## Show this help
	@echo
	@echo "ESPgotchi"
	@echo
	@grep -E '^[a-zA-Z0-9_-]+:.*?## .*$$' $(MAKEFILE_LIST) \
		| awk 'BEGIN {FS = ":.*?## "} {printf "  \033[36m%-10s\033[0m %s\n", $$1, $$2}'
	@echo
	@echo "  Board: $(BOARD) (BOARD=c6 ESP32-C6-LCD-1.47, BOARD=s3 ESP32-S3-Touch-LCD-1.69)"
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

build: gen ## Regenerate assets and compile the firmware (BOARD=c6|s3)
	arduino-cli compile --fqbn "$(FQBN)" --export-binaries --warnings default $(SKETCH)

build-all: gen ## Compile every supported board
	@for b in $(BOARDS); do $(MAKE) --no-print-directory build BOARD=$$b || exit 1; done

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

# The manifest names one set of parts per board: <part>-<board>.bin. CI assembles the same.
og: ## Render the landing page social card to _site/og.png (no dependencies)
	@node tools/gen_og.mjs _site/og.png

site: ## Build every board and assemble the web installer in _site/, same as CI
	@for b in $(BOARDS); do $(MAKE) --no-print-directory build bootapp0 BOARD=$$b || exit 1; done
	@rm -rf _site && mkdir -p _site
	@cp docs/index.html docs/manifest.json _site/
	@cp shared/sprites.json _site/sprites.json
	@node tools/gen_og.mjs _site/og.png
	@for b in $(BOARDS); do \
	  d=$$($(MAKE) --no-print-directory print-build BOARD=$$b); \
	  cp $$d/espgotchi.ino.bin            _site/espgotchi-$$b.bin; \
	  cp $$d/espgotchi.ino.bootloader.bin _site/bootloader-$$b.bin; \
	  cp $$d/espgotchi.ino.partitions.bin _site/partitions-$$b.bin; \
	  cp $$d/boot_app0.bin                _site/boot_app0-$$b.bin; \
	done
	@echo "Installer assembled in _site/"

print-build: ## Print the build directory for BOARD (used by site and CI)
	@echo $(BUILD)

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
