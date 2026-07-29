# Builds m2-sdk.dll — the one shared m2 layer every mod links against — plus the import library
# their linkers bind to.
#
# 32-bit only. Mercenaries 2 is a 32-bit process, so there is no other target worth having.

CC_MINGW ?= i686-w64-mingw32-gcc
BUILDDIR := build
OBJDIR   := $(BUILDDIR)/obj

include sdk.mk

DLL    := $(BUILDDIR)/m2-sdk.dll
IMPLIB := $(BUILDDIR)/libm2-sdk.dll.a
OBJS   := $(patsubst %.c,$(OBJDIR)/%.o,$(subst /,_,$(M2_SRCS)))

# M2_BUILDING_DLL flips M2_API from dllimport to dllexport (see m2/m2_api.h).
CFLAGS  := -O2 -Wall -DM2_BUILDING_DLL $(M2_CFLAGS)
LDFLAGS := -shared -static-libgcc -lkernel32 -luser32

.PHONY: all build ladder ladder-check clean help

all: build
build: $(DLL)

$(DLL): $(M2_SRCS)
	@mkdir -p $(OBJDIR)
	@for src in $(M2_SRCS); do \
		obj=$(OBJDIR)/$$(echo $$src | sed 's|^\./||; s|/|_|g; s|\.c$$|.o|'); \
		echo "  CC $$src"; \
		$(CC_MINGW) $(CFLAGS) -c "$$src" -o "$$obj" || exit 1; \
	done
	@echo "  LD $(DLL)"
	@$(CC_MINGW) $(OBJDIR)/*.o -o $(DLL) $(LDFLAGS) -Wl,--out-implib,$(IMPLIB)
	@echo "built $(DLL) + $(IMPLIB)"

# Regenerate m2/load_ladder.gen.{h,c} from loadprobe's phases.rs. Override with
# PHASES=/path/to/phases.rs if loadprobe is not in one of the known sibling layouts.
ladder:
	python3 gen_ladder.py $(PHASES)

# Drift guard: fail if either generated file no longer matches phases.rs.
#
# CI runs this. The guard existed for months without being wired to anything, which is the same as
# not having it — and drift is silent: m2_loadtrigger would simply stop matching milestones that
# loadprobe had renamed, with no error anywhere.
ladder-check:
	python3 gen_ladder.py $(PHASES) --check

clean:
	rm -rf $(BUILDDIR)

help:
	@echo "make build         — build m2-sdk.dll and its import library (i686 MinGW)"
	@echo "make ladder        — regenerate m2/load_ladder.gen.{h,c} from loadprobe phases.rs"
	@echo "make ladder-check  — verify the generated files match phases.rs (drift guard)"
	@echo "  PHASES=/path/to/phases.rs to override the loadprobe source location"
