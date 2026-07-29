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
#   1. Link with $(M2_LDFLAGS), not $(M2_SRCS). `M2_SRCS` is still defined below, but only so the
#      SDK can build ITSELF — a mod that compiles it in gets a second private copy of MinHook and
#      of the log-stub hook, which is exactly what the shared layer exists to prevent.
#   2. `m2-sdk.dll` must ship BESIDE your .asi. It is a load-time import, so if it is missing the
#      mod does not load at all: LoadLibrary fails with 0x8007007E before any of your code runs, and
#      pmc_bb reports only `[FAILED] <name> (error: 0x...)`.

M2_SDK_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
M2_MINHOOK := $(M2_SDK_DIR)/minhook
M2_BUILD   := $(M2_SDK_DIR)/build

M2_CFLAGS  := -I$(M2_SDK_DIR)/m2 -I$(M2_MINHOOK)/include

# The import library the linker binds against. Built by `make -C $(M2_SDK_DIR)`.
M2_DLL     := $(M2_BUILD)/m2-sdk.dll
M2_IMPLIB  := $(M2_BUILD)/libm2-sdk.dll.a
M2_LDFLAGS := -L$(M2_BUILD) -lm2-sdk

# SDK-internal: the translation units that make up the DLL. A mod should not use this.
M2_SRCS := \
	$(M2_SDK_DIR)/m2/m2_dll.c \
	$(M2_SDK_DIR)/m2/m2_log.c \
	$(M2_SDK_DIR)/m2/m2_ini.c \
	$(M2_SDK_DIR)/m2/m2_hook.c \
	$(M2_SDK_DIR)/m2/m2_luastack.c \
	$(M2_SDK_DIR)/m2/m2_loghook.c \
	$(M2_SDK_DIR)/m2/m2_loadtrigger.c \
	$(M2_SDK_DIR)/m2/load_ladder.gen.c \
	$(M2_MINHOOK)/src/hook.c \
	$(M2_MINHOOK)/src/buffer.c \
	$(M2_MINHOOK)/src/trampoline.c \
	$(M2_MINHOOK)/src/hde/hde32.c
