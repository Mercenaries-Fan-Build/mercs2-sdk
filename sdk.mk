# sdk.mk — include from a mod Makefile to build against the shared Mercs2 mod layer.
#
#   include ../../sdk/sdk.mk
#   $(CC) $(CFLAGS) $(M2_CFLAGS) -o mod.asi mod.c $(M2_LDFLAGS) $(LDFLAGS)
#
# Paths resolve relative to the including Makefile's directory (via MAKEFILE_LIST), so a mod builds
# from its own folder without knowing the SDK's absolute location.
#
# ⚠ The SDK is a SHARED DLL now, not a pile of sources compiled into your mod. Two consequences:
#
#   1. Link with $(M2_LDFLAGS). The DLL is built from wad_simulator's crates/mercs2_mod_runtime, and
#      nothing of it is compiled into a mod: a second private copy of MinHook or of the log-stub hook
#      is exactly what the shared layer exists to prevent.
#   2. `m2-sdk.dll` lives in the GAME ROOT, placed there by the m2-sdk Shipment; your Shipment
#      requires m2-sdk rather than shipping a copy. It is a load-time import, so if it is missing the
#      mod does not load at all: LoadLibrary fails with 0x8007007E before any of your code runs, and
#      pmc_bb reports only `[FAILED] <name> (error: 0x...)`.

M2_SDK_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
M2_BUILD   := $(M2_SDK_DIR)/build

M2_CFLAGS  := -I$(M2_SDK_DIR)/m2

# The import library the linker binds against. Built by `make -C $(M2_SDK_DIR)`.
M2_DLL     := $(M2_BUILD)/m2-sdk.dll
M2_IMPLIB  := $(M2_BUILD)/libm2-sdk.dll.a
M2_LDFLAGS := -L$(M2_BUILD) -lm2-sdk

