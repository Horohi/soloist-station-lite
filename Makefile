CC ?= clang
TARGET ?= aarch64-linux-gnu
CFLAGS ?= --target=$(TARGET) -O2 -fno-builtin -nostdlib -fPIE -Wl,-pie
STRIP ?= llvm-strip

BIN_DIR = bin
SRC_DIR = src
TARGET_BIN = $(BIN_DIR)/soloist-bridge

all: $(TARGET_BIN)

$(TARGET_BIN): $(SRC_DIR)/soloist-bridge.c
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $<
	$(STRIP) -s $@

clean:
	rm -f $(TARGET_BIN)

.PHONY: all clean
