# Builds spawn_registry.asi the same way a real mod does — `include ../../sdk.mk`, link
# $(M2_LDFLAGS) — so if that contract breaks this breaks with it. Run `make build` at the
# SDK root first to produce the import library this links against.

include ../../sdk.mk

CC_MINGW = i686-w64-mingw32-gcc
TARGET   = spawn_registry.asi

.PHONY: all clean verify

all: $(TARGET)

$(TARGET): spawn_registry.c $(M2_IMPLIB)
	$(CC_MINGW) -O2 -Wall -shared $(M2_CFLAGS) -o $@ spawn_registry.c $(M2_LDFLAGS) -lkernel32 -luser32

# The import library only exists once the SDK is built; say so plainly rather than failing inside ld.
$(M2_IMPLIB):
	@echo "error: $(M2_IMPLIB) not found — run 'make build' at the SDK root first" >&2
	@exit 1

# ★ Only the import table proves the ASI binds to the shared layer at load time rather than
# having inlined a private copy of the hook.
verify: $(TARGET)
	@i686-w64-mingw32-objdump -p $(TARGET) | grep -q "DLL Name: m2-sdk.dll" \
		|| { echo "FAIL: $(TARGET) does not import m2-sdk.dll" >&2; exit 1; }
	@i686-w64-mingw32-objdump -p $(TARGET) | grep -q "m2_spawn_registry_install" \
		|| { echo "FAIL: $(TARGET) does not import m2_spawn_registry_install" >&2; exit 1; }
	@echo "OK: $(TARGET) imports m2-sdk.dll and m2_spawn_registry_install"

clean:
	rm -f $(TARGET)
