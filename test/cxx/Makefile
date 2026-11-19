# Builds a C++ consumer the way a C++ mod is built, and checks its import table: every m2 function it
# calls is imported from m2-sdk.dll by its C name, and no mangled C++ name (_Z...) is imported.

include ../../sdk.mk

CXX_MINGW = i686-w64-mingw32-g++
TARGET    = cxx.asi
OUT       = $(M2_BUILD)/test-cxx
IMPORTS   = $(OUT)/cxx.imports.txt

# One function from each header that declares one, as cxx.cpp calls them.
SYMS = m2_lua_ref m2_lua_unref m2_lua_push_ref m2_lua_pcall m2_lua_add_namespace m2_lua_nargs \
       m2_version_num m2_version_string m2_log_init m2_logf_mod m2_hook_init m2_ini_bool \
       m2_loadtrigger_current_phase m2_loghook_add_listener m2_lua_debug_on_paused \
       m2_shader_status_name m2_render_last_error

.PHONY: all clean verify

all: $(TARGET)

$(TARGET): cxx.cpp $(M2_IMPLIB)
	$(CXX_MINGW) -O2 -Wall -Wextra -Werror -shared -static-libgcc -static-libstdc++ $(M2_CFLAGS) -o $@ cxx.cpp $(M2_LDFLAGS) -lkernel32 -luser32

$(M2_IMPLIB):
	@echo "error: $(M2_IMPLIB) not found — run 'make build' at the SDK root first" >&2
	@exit 1

# objdump writes the whole table to a file first, so no reader closes its pipe early.
verify: $(TARGET)
	mkdir -p $(OUT)
	i686-w64-mingw32-objdump -p $(TARGET) > $(IMPORTS)
	@grep -q "DLL Name: m2-sdk.dll" $(IMPORTS) \
		|| { echo "FAIL: $(TARGET) does not import m2-sdk.dll" >&2; exit 1; }
	@for sym in $(SYMS); do \
		grep -q " $$sym$$" $(IMPORTS) \
			|| { echo "FAIL: $(TARGET) does not import $$sym by its C name" >&2; exit 1; }; \
	done
	@if grep -E "^[[:space:]]+[0-9a-f]+[[:space:]]+[0-9]+[[:space:]]+_?_Z" $(IMPORTS); then \
		echo "FAIL: $(TARGET) imports the mangled C++ names above" >&2; exit 1; \
	fi
	@echo "OK: $(TARGET) imports every m2 function it calls by its C name"

clean:
	rm -f $(TARGET)
	rm -rf $(OUT)
