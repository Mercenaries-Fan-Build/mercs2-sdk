# Builds m2_smoke.asi, the in-game smoke test of m2-sdk.dll (see m2_smoke.c). Install it in the game's
# scripts/ folder with m2-sdk.dll in the game root, load into the world, and read scripts/m2_smoke.log.

include ../../sdk.mk

CC_MINGW = i686-w64-mingw32-gcc
TARGET   = m2_smoke.asi

.PHONY: all clean

all: $(TARGET)

$(TARGET): m2_smoke.c $(M2_IMPLIB)
	$(CC_MINGW) -O2 -Wall -Wextra -shared $(M2_CFLAGS) -o $@ m2_smoke.c $(M2_LDFLAGS) -lkernel32 -luser32

$(M2_IMPLIB):
	@echo "error: $(M2_IMPLIB) not found — run 'make build' at the SDK root first" >&2
	@exit 1

clean:
	rm -f $(TARGET)
