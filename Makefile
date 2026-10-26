CC = aarch64-linux-gnu-gcc
STRIP = aarch64-linux-gnu-strip
CFLAGS ?= -O2 -nostdlib -static -fno-stack-protector -fno-builtin

BIN_DIR = bin
SRC_DIR = src
TARGET_BIN = $(BIN_DIR)/soloist-bridge
MP3_BIN = $(BIN_DIR)/mp32wav

all: $(TARGET_BIN) $(MP3_BIN)

$(TARGET_BIN): $(SRC_DIR)/soloist-bridge.c
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $<
	$(STRIP) -s $@

$(MP3_BIN): $(SRC_DIR)/mp32wav.c
	mkdir -p $(BIN_DIR)
	$(CC) -O2 -static -I$(SRC_DIR) -o $@ $< -lm
	$(STRIP) -s $@

clean:
	rm -f $(TARGET_BIN) $(MP3_BIN)

.PHONY: all clean
