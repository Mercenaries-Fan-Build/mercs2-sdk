# Checks the SDK version a mod compiles against. Needs only the 32-bit MinGW compiler: no DLL, no
# rustup, no network, so it runs before `make build`.
#
#   make -C test/version                                          every check below
#   make -C test/version headers HEADERS=<dir> EXPECT_VERSION=x.y.z
#                                     the headers in <dir> alone compile M2_VERSION_NUM for x.y.z
#                                     (the release runs this on the headers it zips)
#
# Every check is gated on the compiler's or the script's exit code.

include ../../sdk.mk

CC_MINGW = i686-w64-mingw32-gcc
OUT      = $(M2_BUILD)/test-version
STAMP    = bash $(M2_SDK_DIR)/scripts/stamp_version_header.sh

# -DM2_TEST_MAJOR=x -DM2_TEST_MINOR=y -DM2_TEST_PATCH=z for $(1) = x.y.z, which version.c asserts.
m2_test_defs = -DM2_TEST_MAJOR=$(word 1,$(subst ., ,$(1))) -DM2_TEST_MINOR=$(word 2,$(subst ., ,$(1))) \
               -DM2_TEST_PATCH=$(word 3,$(subst ., ,$(1)))

.PHONY: all no-version sdk-mk stamp stamp-rejects headers clean

all: no-version sdk-mk stamp stamp-rejects

$(OUT):
	mkdir -p $@

# The m2/ headers alone, with no version from sdk.mk or a stamp, must not compile.
no-version: | $(OUT)
	@if $(CC_MINGW) -fsyntax-only -I$(M2_SDK_DIR)/m2 no_version.c 2> $(OUT)/no_version.err; then \
		echo "FAIL: m2/m2_version.h compiled with no SDK version, so m2_abi_ok() accepts every 0.x m2-sdk.dll" >&2; \
		exit 1; \
	fi
	@grep -qF 'm2_version.h: no SDK version' $(OUT)/no_version.err \
		|| { echo "FAIL: no_version.c failed for a reason other than the missing version:" >&2; cat $(OUT)/no_version.err >&2; exit 1; }
	@echo "OK: the m2/ headers with no version stop at the #error"

# A mod built through sdk.mk compiles the version in manifest.yaml.
sdk-mk:
	@test -n "$(M2_SDK_VERSION)" || { echo "FAIL: sdk.mk sets no M2_SDK_VERSION" >&2; exit 1; }
	@grep -Eqx '[[:space:]]*version:[[:space:]]*$(M2_SDK_VERSION)[[:space:]]*' $(M2_SDK_DIR)/manifest.yaml \
		|| { echo "FAIL: manifest.yaml has no 'version: $(M2_SDK_VERSION)' line" >&2; exit 1; }
	$(CC_MINGW) -fsyntax-only $(M2_CFLAGS) $(call m2_test_defs,$(M2_SDK_VERSION)) version.c
	@echo "OK: \$$(M2_CFLAGS) compiles m2 $(M2_SDK_VERSION)"

# The release's headers zip: the stamped copy compiles the version with no sdk.mk flags at all.
stamp: | $(OUT)
	rm -rf $(OUT)/headers
	$(STAMP) $(M2_SDK_VERSION) $(M2_SDK_DIR)/m2 $(OUT)/headers
	$(MAKE) --no-print-directory headers HEADERS=$(OUT)/headers EXPECT_VERSION=$(M2_SDK_VERSION)

# The stamp refuses a version m2_version_num() cannot carry and a header without exactly one marker.
stamp-rejects: | $(OUT)
	@for v in 0.4 0.4.0.1 x.y.z 0.100.0 0.4.100 0.04.0 v0.4.0; do \
		if $(STAMP) $$v $(M2_SDK_DIR)/m2 $(OUT)/rejected 2> $(OUT)/stamp_reject.err; then \
			echo "FAIL: the stamp accepted version '$$v'" >&2; exit 1; \
		fi; \
	done
	@rm -rf $(OUT)/nomarker && mkdir -p $(OUT)/nomarker
	@printf '#ifndef M2_VERSION_H\n#define M2_VERSION_H\n#endif\n' > $(OUT)/nomarker/m2_version.h
	@if $(STAMP) 0.4.0 $(OUT)/nomarker $(OUT)/rejected 2> $(OUT)/stamp_reject.err; then \
		echo "FAIL: the stamp accepted an m2_version.h with no marker line" >&2; exit 1; \
	fi
	@echo "OK: the stamp refuses malformed versions and a header without its marker"

headers:
	@test -n "$(HEADERS)" || { echo "error: set HEADERS=<dir of stamped headers>" >&2; exit 1; }
	@test -n "$(EXPECT_VERSION)" || { echo "error: set EXPECT_VERSION=x.y.z" >&2; exit 1; }
	$(CC_MINGW) -fsyntax-only -I$(HEADERS) $(call m2_test_defs,$(EXPECT_VERSION)) version.c
	@echo "OK: the headers in $(HEADERS) compile m2 $(EXPECT_VERSION)"

clean:
	rm -rf $(OUT)
