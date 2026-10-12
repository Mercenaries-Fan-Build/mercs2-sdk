# Runs runtime.exe against an m2-sdk.dll under Wine and, with BASELINE_DLL, against a second DLL (a
# released m2-sdk.dll) and diffs the two outputs, which must match byte for byte.
#
#   make -C test/runtime run WINE=/path/to/wine
#   make -C test/runtime compare WINE=/path/to/wine BASELINE_DLL=/path/to/released/m2-sdk.dll

include ../../sdk.mk

CC_MINGW = i686-w64-mingw32-gcc
EXE      = runtime.exe

.PHONY: all run compare clean

all: $(EXE)

$(EXE): runtime.c $(M2_IMPLIB)
	$(CC_MINGW) -O2 -Wall $(M2_CFLAGS) -o $@ runtime.c $(M2_LDFLAGS) -lkernel32 -luser32

$(M2_IMPLIB):
	@echo "error: $(M2_IMPLIB) not found — run 'make build' at the SDK root first" >&2
	@exit 1

define RUN_IN
	rm -rf $(1) && mkdir -p $(1) && cp $(EXE) $(1)/ && cp $(2) $(1)/m2-sdk.dll
	cd $(1) && WINEPREFIX=$(abspath $(1))/prefix WINEDEBUG=-all "$(WINE)" $(EXE) > stdout.txt
	@test -s $(1)/stdout.txt || { echo "FAIL: $(1)/stdout.txt is empty" >&2; exit 1; }
endef

run: $(EXE)
	@test -n "$(WINE)" || { echo "error: set WINE=/path/to/wine" >&2; exit 1; }
	$(call RUN_IN,out/current,$(M2_DLL))
	cat out/current/stdout.txt out/current/runtime.log

compare: $(EXE)
	@test -n "$(WINE)" || { echo "error: set WINE=/path/to/wine" >&2; exit 1; }
	@test -f "$(BASELINE_DLL)" || { echo "error: set BASELINE_DLL=/path/to/m2-sdk.dll" >&2; exit 1; }
	$(call RUN_IN,out/current,$(M2_DLL))
	$(call RUN_IN,out/baseline,$(BASELINE_DLL))
	diff out/baseline/stdout.txt out/current/stdout.txt
	diff out/baseline/runtime.log out/current/runtime.log
	@echo "OK: stdout and runtime.log match the baseline"

clean:
	rm -rf $(EXE) out
