# tty-buddy — local release builds
#
#   make firmware     # versioned merged factory .bin → dist/
#   make daemon       # .deb + tarball for this host arch → dist/
#   make daemon-all   # .deb + tarball for amd64 and arm64
#   make release      # firmware + daemon (host arch)
#   make release-all  # firmware + daemon-all
#   make clean

ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
VERSION ?= $(shell sed -n 's/^version = "\(.*\)"/\1/p' "$(ROOT)/daemon/Cargo.toml" | head -1)
DIST    := $(ROOT)/dist

FW_ENV  := esp32-c3-supermini
FW_RAW  := $(ROOT)/firmware/dist/tty-buddy-firmware-$(VERSION).bin
FW_OUT  := $(DIST)/tty-buddy-firmware-$(VERSION).bin

HOST_M := $(shell uname -m)
ifeq ($(HOST_M),x86_64)
HOST_DEB_ARCH := amd64
HOST_TAR_ARCH := x86_64
else ifeq ($(HOST_M),amd64)
HOST_DEB_ARCH := amd64
HOST_TAR_ARCH := x86_64
else ifeq ($(HOST_M),aarch64)
HOST_DEB_ARCH := arm64
HOST_TAR_ARCH := aarch64
else ifeq ($(HOST_M),arm64)
HOST_DEB_ARCH := arm64
HOST_TAR_ARCH := aarch64
else
HOST_DEB_ARCH :=
HOST_TAR_ARCH :=
endif

.PHONY: all help version firmware daemon daemon-all release release-all checksums clean

all: release

help:
	@echo "VERSION=$(VERSION)"
	@echo "  make firmware     — PlatformIO build + merged factory .bin"
	@echo "  make daemon       — .deb + tarball for host arch ($(HOST_DEB_ARCH))"
	@echo "  make daemon-all   — .deb + tarball for amd64 and arm64"
	@echo "  make release      — firmware + daemon + checksums.txt"
	@echo "  make release-all  — firmware + daemon-all + checksums.txt"
	@echo "  make checksums    — SHA-256 of artifacts already in dist/"
	@echo "  make clean        — remove dist/ build outputs"

version:
	@echo $(VERSION)

firmware:
	@test -n "$(VERSION)" || (echo "could not read version from daemon/Cargo.toml" >&2; exit 1)
	cd "$(ROOT)/firmware" && pio run -e $(FW_ENV)
	@test -f "$(FW_RAW)" || (echo "missing $(FW_RAW) — merge_factory post-script failed?" >&2; exit 1)
	mkdir -p "$(DIST)"
	cp -f "$(FW_RAW)" "$(FW_OUT)"
	@ls -lh "$(FW_OUT)"

daemon:
	@test -n "$(VERSION)" || (echo "could not read version from daemon/Cargo.toml" >&2; exit 1)
	@test -n "$(HOST_DEB_ARCH)" || (echo "unsupported host arch: $(HOST_M)" >&2; exit 1)
	DEST="$(DIST)" "$(ROOT)/daemon/packaging/build-deb.sh" "$(HOST_DEB_ARCH)"
	DEST="$(DIST)" "$(ROOT)/daemon/packaging/build-linux-release.sh" "$(HOST_DEB_ARCH)"
	@ls -lh \
	  "$(DIST)/tty-buddy_$(VERSION)_$(HOST_DEB_ARCH).deb" \
	  "$(DIST)/tty-buddy-$(VERSION)-$(HOST_TAR_ARCH)-linux.tar.gz"

daemon-all:
	@test -n "$(VERSION)" || (echo "could not read version from daemon/Cargo.toml" >&2; exit 1)
	DEST="$(DIST)" "$(ROOT)/daemon/packaging/build-deb.sh" amd64
	DEST="$(DIST)" "$(ROOT)/daemon/packaging/build-linux-release.sh" amd64
	DEST="$(DIST)" "$(ROOT)/daemon/packaging/build-deb.sh" arm64
	DEST="$(DIST)" "$(ROOT)/daemon/packaging/build-linux-release.sh" arm64
	@ls -lh \
	  "$(DIST)/tty-buddy_$(VERSION)_amd64.deb" \
	  "$(DIST)/tty-buddy_$(VERSION)_arm64.deb" \
	  "$(DIST)/tty-buddy-$(VERSION)-x86_64-linux.tar.gz" \
	  "$(DIST)/tty-buddy-$(VERSION)-aarch64-linux.tar.gz"

checksums:
	@test -d "$(DIST)" || (echo "missing $(DIST)" >&2; exit 1)
	chmod +x "$(ROOT)/scripts/write-checksums.sh"
	"$(ROOT)/scripts/write-checksums.sh" "$(DIST)"

release: firmware daemon checksums
	@echo "==> release $(VERSION) (host $(HOST_DEB_ARCH))"
	@ls -lh "$(DIST)"

release-all: firmware daemon-all checksums
	@echo "==> release-all $(VERSION)"
	@ls -lh "$(DIST)"

clean:
	rm -rf "$(DIST)" "$(ROOT)/firmware/dist" "$(ROOT)/daemon/dist"
	cd "$(ROOT)/firmware" && pio run -t clean -e $(FW_ENV) 2>/dev/null || true
	cd "$(ROOT)/daemon" && cargo clean 2>/dev/null || true
