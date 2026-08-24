BUILD_DIR ?= build
BUILD_TYPE ?= Release
# The bundle installs into /Applications so Launch Services and the login item
# can find it by a stable path; TCC grants follow the signature, not the path,
# but a moving app still confuses everything else.
PREFIX ?= /Applications
GENERATOR ?= Ninja
# Homebrew's Qt is not in CMake's default search path.
QT_PREFIX ?= $(shell brew --prefix qt 2>/dev/null)
# "-" is ad-hoc: no stable identity, so macOS re-asks for Screen Recording on
# every rebuild. Override with a real identity to keep the grant.
FOMOSNAP_CODESIGN_IDENTITY ?= -
APP := $(BUILD_DIR)/FOMOsnap.app

CMAKE ?= cmake
CLANG_TIDY ?= clang-tidy
# Homebrew's clang-tidy is not Apple's clang and does not find the macOS SDK on
# its own, and CMake does not put -isysroot in compile_commands.json when the
# default Apple toolchain is used. Without this, every check fails on a missing
# <type_traits>.
SDKROOT_PATH ?= $(shell xcrun --show-sdk-path)
CLAZY ?= clazy-standalone
QMLLINT ?= qmllint

LINT_SOURCES := $(wildcard src/*.cpp tests/*.cpp)
# Objective-C++ is excluded: clang-tidy's C++ checks mis-parse Apple headers,
# and the platform layer is small enough to review by eye.
LINT_CHECKS ?= -*,clang-analyzer-*,bugprone-*,performance-*,misc-*

.PHONY: all configure build clean check cask-check signing-check install smoke lint qt-lint run agent icon

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -G $(GENERATOR) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		-DCMAKE_PREFIX_PATH="$(QT_PREFIX)" \
		-DFOMOSNAP_CODESIGN_IDENTITY="$(FOMOSNAP_CODESIGN_IDENTITY)" \
		-DCMAKE_INSTALL_PREFIX=$(PREFIX)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel

smoke: build
	QT_QPA_PLATFORM=offscreen $(BUILD_DIR)/fomosnap-smoke \
		$(BUILD_DIR)/fomosnap-smoke-output

lint: build
	@set -eu; \
	if command -v "$(CLANG_TIDY)" >/dev/null 2>&1; then \
		commands_dir=$$(mktemp -d); \
		trap 'rm -rf "$$commands_dir"' EXIT; \
		cp "$(BUILD_DIR)/compile_commands.json" \
			"$$commands_dir/compile_commands.json"; \
		for source in $(LINT_SOURCES); do \
			"$(CLANG_TIDY)" -p "$$commands_dir" \
				--extra-arg=-isysroot --extra-arg="$(SDKROOT_PATH)" \
				-checks="$(LINT_CHECKS)" -header-filter='.*' "$$source"; \
		done; \
	else \
		echo "make check: clang-tidy unavailable; skipping"; \
	fi

qt-lint: build
	@set -eu; \
	if command -v "$(CLAZY)" >/dev/null 2>&1; then \
		for source in $(LINT_SOURCES); do \
			"$(CLAZY)" -p "$(BUILD_DIR)" "$$source"; \
		done; \
	else \
		echo "make check: clazy unavailable; skipping Qt-specific clazy pass"; \
	fi; \
	if command -v "$(QMLLINT)" >/dev/null 2>&1 && test -d qml; then \
		"$(QMLLINT)" qml; \
	else \
		echo "make check: no QML sources or qmllint unavailable; skipping"; \
	fi

cask-check:
	ruby tests/homebrew-cask-smoke.rb packaging/homebrew/fomosnap.rb

signing-check:
	ruby tests/release-signing-smoke.rb .github/workflows/build-macos.yml

check: cask-check signing-check smoke lint qt-lint

clean:
	@if test -d "$(BUILD_DIR)"; then \
		$(CMAKE) --build "$(BUILD_DIR)" --target clean; \
	fi

install: build
	$(CMAKE) --install $(BUILD_DIR)

# Redraws the app icon from tools/icon-generator.cpp. The .icns is committed,
# so this only needs running when the artwork changes.
icon: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel --target icon-generator
	rm -rf $(BUILD_DIR)/FOMOsnap.iconset
	$(BUILD_DIR)/icon-generator $(BUILD_DIR)/FOMOsnap.iconset
	iconutil -c icns $(BUILD_DIR)/FOMOsnap.iconset -o assets/FOMOsnap.icns

# Opens the overlay once, the way a hotkey would.
run: build
	$(APP)/Contents/MacOS/FOMOsnap $(ARGS)

# Runs the resident agent in the foreground so its logging is visible.
agent: build
	$(APP)/Contents/MacOS/FOMOsnap --agent $(ARGS)
