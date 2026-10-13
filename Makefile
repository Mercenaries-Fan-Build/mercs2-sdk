# Builds m2-sdk.dll and its import library from the published mercs2_mod_runtime crate into build/,
# and checks m2/load_ladder.gen.h against the header that build writes.
#
#   make build VERSION=0.4.0
#
# 32-bit only. Mercenaries 2 is a 32-bit process, so there is no other target worth having.

TARGET   := i686-pc-windows-gnu
BUILDDIR := build
DLL      := $(BUILDDIR)/m2-sdk.dll
IMPLIB   := $(BUILDDIR)/libm2-sdk.dll.a
HEADER   := $(BUILDDIR)/load_ladder.gen.h

# The mercs2_mod_runtime release the DLL is built from. scripts/fetch_runtime.sh downloads it from
# crates.io and checks it against the sha256 the index records.
RUNTIME_VERSION := 0.4.0
RUNTIME         := $(BUILDDIR)/mercs2_mod_runtime-$(RUNTIME_VERSION)

# The release pipeline passes the tag without its `v`. A build without VERSION reports 0.0.0, a version
# no release carries. manifest.yaml carries the same version by hand, and the release workflow refuses
# to publish when the two differ.
VERSION ?= 0.0.0

.PHONY: all build ladder clean help

all: build

# cargo reads rust-toolchain.toml and .cargo/config.toml from the directory it runs in, so the build
# runs inside the unpacked crate. Its scripts/fetch_mingw_libs.sh places the rust-mingw libraries the
# link uses. --locked builds the dependency versions the crate was published with.
build:
	@mkdir -p $(BUILDDIR)
	rm -f $(DLL) $(IMPLIB) $(HEADER)
	bash scripts/fetch_runtime.sh $(RUNTIME_VERSION) $(BUILDDIR)
	cd "$(RUNTIME)" && bash scripts/fetch_mingw_libs.sh
	cd "$(RUNTIME)" && M2_VERSION=$(VERSION) M2_IMPLIB=$(abspath $(IMPLIB)) cargo build --locked --release --target $(TARGET)
	cp "$(RUNTIME)/target/$(TARGET)/release/mercs2_mod_runtime.dll" $(DLL)
	@test -f $(IMPLIB) || { echo "error: $(IMPLIB) was not written by the link" >&2; exit 1; }
	@test -f $(HEADER) || { echo "error: $(HEADER) was not written by the build" >&2; exit 1; }
	@cmp -s $(HEADER) m2/load_ladder.gen.h || { echo "error: m2/load_ladder.gen.h differs from the DLL's ladder ($(HEADER)); run 'make ladder'" >&2; exit 1; }
	@echo "built $(DLL) + $(IMPLIB) from mercs2_mod_runtime $(RUNTIME_VERSION)"

# Replace m2/load_ladder.gen.h with the header the last build wrote.
ladder:
	@test -f $(HEADER) || { echo "error: $(HEADER) not found; run 'make build' first" >&2; exit 1; }
	cp $(HEADER) m2/load_ladder.gen.h

clean:
	rm -rf $(BUILDDIR)

help:
	@echo "make build [VERSION=x.y.z]  build/m2-sdk.dll + build/libm2-sdk.dll.a from mercs2_mod_runtime $(RUNTIME_VERSION)"
	@echo "make ladder                 update m2/load_ladder.gen.h from the last build"
