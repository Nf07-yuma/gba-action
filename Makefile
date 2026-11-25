# STEEL CHAIN - GBA アクションゲーム
# 必要: gcc-arm-none-eabi, python3   (テスト: libmgba-dev, zlib1g-dev, gcc)

TARGET  := steel_chain
BUILD   := build
GEN     := $(BUILD)/gen
PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy

ARCH    := -mthumb -mthumb-interwork -mcpu=arm7tdmi -mtune=arm7tdmi
CFLAGS  := $(ARCH) -O2 -g -Wall -Wextra -Wno-unused-parameter -ffreestanding -fno-strict-aliasing \
           -fomit-frame-pointer -ffunction-sections -fdata-sections -Iinclude -I$(GEN) -Isrc
LDFLAGS := $(ARCH) -nostartfiles -nostdlib -T gba.ld -Wl,--gc-sections -Wl,-Map,$(BUILD)/$(TARGET).map

SRCS    := $(wildcard src/*.c)
OBJS    := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS)) $(BUILD)/assets.o $(BUILD)/crt0.o
ASSET_SRC := $(wildcard assets/*.txt assets/levels/*.txt assets/music/*.txt) tools/gen_assets.py

.PHONY: all clean test assets sprites FORCE

all: $(TARGET).gba

$(GEN)/assets.c $(GEN)/assets.h: $(ASSET_SRC)
	@mkdir -p $(GEN)
	python3 tools/gen_assets.py $(GEN)

# 主人公/エフェクトのスプライトを再合成 (手動実行)
sprites:
	python3 tools/compose_player.py
	python3 tools/compose_fx.py
	python3 tools/compose_boss.py

$(BUILD)/%.o: src/%.c $(GEN)/assets.h src/game.h include/gba.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# ビルド番号 (git の短いハッシュ)。内容が変わったときだけ書き換えて無駄な再ビルドを避ける
$(GEN)/version.h: FORCE
	@mkdir -p $(GEN)
	@v=$$(git rev-parse --short=7 HEAD 2>/dev/null || echo dev); \
	 echo "#define GAME_VERSION \"$$v\"" > $@.new; cmp -s $@.new $@ || cp $@.new $@
$(BUILD)/title.o: $(GEN)/version.h

$(BUILD)/assets.o: $(GEN)/assets.c $(GEN)/assets.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/crt0.o: src/crt0.s
	@mkdir -p $(BUILD)
	$(CC) $(ARCH) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) gba.ld
	$(CC) $(LDFLAGS) $(OBJS) -lgcc -o $@

$(TARGET).gba: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	python3 tools/fixheader.py $@

# ---- ヘッドレステスト (mGBA コア) ----
$(BUILD)/runner: tests/runner.c
	@mkdir -p $(BUILD)
	gcc -O2 -Wall -o $@ $< -lmgba -lz -lm

test: $(TARGET).gba $(BUILD)/runner
	$(BUILD)/runner $(TARGET).gba $(BUILD)/test-out

clean:
	rm -rf $(BUILD) $(TARGET).gba
