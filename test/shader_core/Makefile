# Host tests for the shader registry's core. m2/m2_shader_core.c has no Windows dependency, so it
# builds with the host compiler and runs here; the Windows side (m2/m2_shader.c) builds into
# m2-sdk.dll and runs only inside the game.

SDK_DIR  := ../..
CC       ?= cc
CFLAGS   := -O2 -Wall -Wextra -Werror -I$(SDK_DIR)/m2
TARGET   := test_shader_core

.PHONY: all check clean

all: $(TARGET)

$(TARGET): test_shader_core.c $(SDK_DIR)/m2/m2_shader_core.c $(SDK_DIR)/m2/m2_shader_core.h \
           $(SDK_DIR)/m2/m2_shader_types.h $(SDK_DIR)/m2/m2_target.h
	$(CC) $(CFLAGS) -o $@ test_shader_core.c $(SDK_DIR)/m2/m2_shader_core.c

check: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
