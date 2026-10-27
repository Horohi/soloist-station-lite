CC = aarch64-linux-gnu-gcc
STRIP = aarch64-linux-gnu-strip
CFLAGS ?= -O2 -nostdlib -static -fno-stack-protector -fno-builtin

BIN_DIR = bin
SRC_DIR = src
TARGET_BIN = $(BIN_DIR)/soloist-bridge

all: $(TARGET_BIN)

$(TARGET_BIN): $(SRC_DIR)/soloist-bridge.c $(SRC_DIR)/voice-request.h
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $<
	$(STRIP) -s $@

clean:
	rm -f $(TARGET_BIN)

.PHONY: all clean
