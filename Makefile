ifeq ($(shell which aarch64-linux-gnu-gcc 2>/dev/null),)
  CC = clang --target=aarch64-linux-gnu -fuse-ld=lld
  STRIP = llvm-strip
else
  CC = aarch64-linux-gnu-gcc
  STRIP = aarch64-linux-gnu-strip
endif
CFLAGS ?= -O2 -nostdlib -static -fno-stack-protector -fno-builtin

BIN_DIR = bin
SRC_DIR = src
ALSA_LIB_DIR = lib64/alsa-lib

TARGET_BIN = $(BIN_DIR)/soloist-bridge
TARGET_ALSA_SHIM = $(ALSA_LIB_DIR)/libasound_module_rate_speexrate_medium.so

all: $(TARGET_BIN) $(TARGET_ALSA_SHIM)

$(TARGET_BIN): $(SRC_DIR)/soloist-bridge.c $(SRC_DIR)/voice-request.h
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $<
	$(STRIP) -s $@

$(TARGET_ALSA_SHIM): $(SRC_DIR)/rate_samplerate.c
	mkdir -p $(ALSA_LIB_DIR)
	$(CC) -shared -fPIC -O2 -nostdlib -o $@ $<
	$(STRIP) -s $@
	cd $(ALSA_LIB_DIR) && \
	ln -sfn libasound_module_rate_speexrate_medium.so libasound_module_rate_speexrate.so && \
	ln -sfn libasound_module_rate_speexrate_medium.so libasound_module_rate_speexrate_best.so && \
	ln -sfn libasound_module_rate_speexrate_medium.so libasound_module_rate_speexrate_order.so && \
	ln -sfn libasound_module_rate_speexrate_medium.so libasound_module_rate_speexrate_linear.so && \
	ln -sfn libasound_module_rate_speexrate_medium.so libasound_module_rate_samplerate.so && \
	ln -sfn libasound_module_rate_speexrate_medium.so libasound_module_rate_samplerate_medium.so && \
	ln -sfn libasound_module_rate_speexrate_medium.so libasound_module_rate_samplerate_best.so && \
	ln -sfn libasound_module_rate_speexrate_medium.so libasound_module_rate_samplerate_order.so && \
	ln -sfn libasound_module_rate_speexrate_medium.so libasound_module_rate_samplerate_linear.so

clean:
	rm -rf $(TARGET_BIN) $(ALSA_LIB_DIR) soloist.yampkg

pack-yam: $(TARGET_BIN) $(TARGET_ALSA_SHIM)
	yam --platform yandexmicro pack . --output soloist.yampkg

.PHONY: all clean pack-yam
