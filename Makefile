# Builds m2-sdk.dll and its import library from mercs2_mod_runtime, the crate in a mercs2-wad-simulator
# checkout, into build/, and checks m2/load_ladder.gen.h against the header that build writes.
#
#   make build WAD_SIMULATOR=/path/to/mercs2-wad-simulator VERSION=0.4.0
#
# 32-bit only. Mercenaries 2 is a 32-bit process, so there is no other target worth having.

TARGET   := i686-pc-windows-gnu
BUILDDIR := build
DLL      := $(BUILDDIR)/m2-sdk.dll
IMPLIB   := $(BUILDDIR)/libm2-sdk.dll.a
HEADER   := $(BUILDDIR)/load_ladder.gen.h
RUNTIME   = $(WAD_SIMULATOR)/crates/mercs2_mod_runtime

# The release pipeline passes the tag without its `v`. A build without VERSION reports 0.0.0, a version
# no release carries. manifest.yaml carries the same version by hand, and the release workflow refuses
# to publish when the two differ.
VERSION ?= 0.0.0

.PHONY: all build ladder clean help require-wad-simulator

all: build

require-wad-simulator:
	@test -n "$(WAD_SIMULATOR)" || { echo "error: set WAD_SIMULATOR=/path/to/mercs2-wad-simulator" >&2; exit 1; }
	@test -f "$(RUNTIME)/Cargo.toml" || { echo "error: $(RUNTIME)/Cargo.toml not found" >&2; exit 1; }

# cargo reads rust-toolchain.toml and .cargo/config.toml from the directory it runs in, so the build
# runs inside the crate. scripts/fetch_mingw_libs.sh places the rust-mingw libraries the link uses.
build: require-wad-simulator
	@mkdir -p $(BUILDDIR)
	rm -f $(DLL) $(IMPLIB) $(HEADER)
	cd "$(RUNTIME)" && bash scripts/fetch_mingw_libs.sh
	cd "$(RUNTIME)" && M2_VERSION=$(VERSION) M2_IMPLIB=$(abspath $(IMPLIB)) cargo build --release --target $(TARGET)
	cp "$(RUNTIME)/target/$(TARGET)/release/mercs2_mod_runtime.dll" $(DLL)
	@test -f $(IMPLIB) || { echo "error: $(IMPLIB) was not written by the link" >&2; exit 1; }
	@test -f $(HEADER) || { echo "error: $(HEADER) was not written by the build" >&2; exit 1; }
	@cmp -s $(HEADER) m2/load_ladder.gen.h || { echo "error: m2/load_ladder.gen.h differs from the DLL's ladder ($(HEADER)); run 'make ladder'" >&2; exit 1; }
	@echo "built $(DLL) + $(IMPLIB)"

# Replace m2/load_ladder.gen.h with the header the last build wrote.
ladder:
	@test -f $(HEADER) || { echo "error: $(HEADER) not found; run 'make build' first" >&2; exit 1; }
	cp $(HEADER) m2/load_ladder.gen.h

clean:
	rm -rf $(BUILDDIR)

help:
	@echo "make build WAD_SIMULATOR=... [VERSION=x.y.z]  build/m2-sdk.dll + build/libm2-sdk.dll.a"
	@echo "make ladder                                   update m2/load_ladder.gen.h from the last build"
