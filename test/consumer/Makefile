# Builds the consumer smoke test the same way a real mod does — including the `include ../../sdk.mk`
# spelling, so if that contract breaks this breaks with it.

include ../../sdk.mk

CC_MINGW = i686-w64-mingw32-gcc
TARGET   = consumer.asi

.PHONY: all clean verify

all: $(TARGET)

$(TARGET): consumer.c $(M2_IMPLIB)
	$(CC_MINGW) -O2 -Wall -shared $(M2_CFLAGS) -o $@ consumer.c $(M2_LDFLAGS) -lkernel32 -luser32

# The import library only exists once the SDK is built; say so plainly rather than failing inside ld.
$(M2_IMPLIB):
	@echo "error: $(M2_IMPLIB) not found — run 'make build' at the SDK root first" >&2
	@exit 1

# ★ The assertion that matters. Compiling proves the headers agree; only the import table proves the
# mod will actually bind to the shared layer at load time rather than having inlined a private copy.
verify: $(TARGET)
	@i686-w64-mingw32-objdump -p $(TARGET) | grep -q "DLL Name: m2-sdk.dll" \
		|| { echo "FAIL: $(TARGET) does not import m2-sdk.dll" >&2; exit 1; }
	@i686-w64-mingw32-objdump -p $(TARGET) | grep -q "m2_loadtrigger_on_phase" \
		|| { echo "FAIL: $(TARGET) does not import m2_loadtrigger_on_phase" >&2; exit 1; }
	@echo "OK: $(TARGET) imports m2-sdk.dll"

clean:
	rm -f $(TARGET)
