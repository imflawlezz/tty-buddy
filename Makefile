# tty-buddy — local release builds
#
#   make firmware   # merged factory .bin (versioned)
#   make daemon     # Linux x86_64 daemon tarball
#   make release    # both → dist/
#   make clean

ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
VERSION ?= $(shell sed -n 's/^version = "\(.*\)"/\1/p' "$(ROOT)/daemon/Cargo.toml" | head -1)
DIST    := $(ROOT)/dist

FW_ENV  := esp32-c3-supermini
FW_RAW  := $(ROOT)/firmware/dist/tty-buddy-firmware.bin
FW_OUT  := $(DIST)/tty-buddy-firmware-$(VERSION).bin

DAEMON_TGZ := $(ROOT)/daemon/dist/tty-buddy-$(VERSION)-x86_64-linux.tar.gz
DAEMON_OUT := $(DIST)/tty-buddy-$(VERSION)-x86_64-linux.tar.gz

.PHONY: all help version firmware daemon release clean

all: release

help:
	@echo "VERSION=$(VERSION)"
	@echo "  make firmware  — PlatformIO build + merged factory .bin"
	@echo "  make daemon    — Linux x86_64 daemon install tarball"
	@echo "  make release   — both artifacts into dist/"
	@echo "  make clean     — remove dist/ build outputs"

version:
	@echo $(VERSION)

firmware:
	@test -n "$(VERSION)" || (echo "could not read version from daemon/Cargo.toml" >&2; exit 1)
	cd "$(ROOT)/firmware" && pio run -e $(FW_ENV)
	@test -f "$(FW_RAW)" || (echo "missing $(FW_RAW) — merge_factory post-script failed?" >&2; exit 1)
	mkdir -p "$(DIST)"
	cp -f "$(FW_RAW)" "$(FW_OUT)"
	cp -f "$(FW_RAW)" "$(DIST)/tty-buddy-firmware.bin"
	@ls -lh "$(FW_OUT)" "$(DIST)/tty-buddy-firmware.bin"

daemon:
	@test -n "$(VERSION)" || (echo "could not read version from daemon/Cargo.toml" >&2; exit 1)
	"$(ROOT)/daemon/packaging/build-linux-release.sh"
	@test -f "$(DAEMON_TGZ)" || (echo "missing $(DAEMON_TGZ)" >&2; exit 1)
	mkdir -p "$(DIST)"
	cp -f "$(DAEMON_TGZ)" "$(DAEMON_OUT)"
	@ls -lh "$(DAEMON_OUT)"

release: firmware daemon
	@echo "==> release $(VERSION)"
	@ls -lh "$(FW_OUT)" "$(DAEMON_OUT)"

clean:
	rm -rf "$(DIST)" "$(ROOT)/firmware/dist" "$(ROOT)/daemon/dist"
	cd "$(ROOT)/firmware" && pio run -t clean -e $(FW_ENV) 2>/dev/null || true
	cd "$(ROOT)/daemon" && cargo clean 2>/dev/null || true
