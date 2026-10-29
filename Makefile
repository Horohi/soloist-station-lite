CC ?= aarch64-linux-gnu-gcc
STRIP ?= aarch64-linux-gnu-strip
CFLAGS ?= -O2 -nostdlib -static -fno-stack-protector -fno-builtin

BIN_DIR = bin
SRC_DIR = src
ALSA_LIB_DIR = lib64/alsa-lib

TARGET_BIN = $(BIN_DIR)/soloist-bridge
TARGET_ALSA_SHIM = $(ALSA_LIB_DIR)/libasound_module_rate_samplerate.so

all: $(TARGET_BIN) $(TARGET_ALSA_SHIM)

$(TARGET_BIN): $(SRC_DIR)/soloist-bridge.c $(SRC_DIR)/voice-request.h
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $<
	$(STRIP) -s $@

$(TARGET_ALSA_SHIM): $(SRC_DIR)/rate_samplerate.c
	mkdir -p $(ALSA_LIB_DIR)
	$(CC) -shared -fPIC -O2 -nostdlib -o $@ $<
	$(STRIP) -s $@

clean:
	rm -rf $(TARGET_BIN) $(ALSA_LIB_DIR) soloist.yampkg

pack-yam: $(TARGET_BIN) $(TARGET_ALSA_SHIM)
	yam --platform yandexmicro pack . --output soloist.yampkg

.PHONY: all clean pack-yam
