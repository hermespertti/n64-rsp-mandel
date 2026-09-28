# ChargeBay N64 — libdragon project (example-style)
# build:  make           -> demo.z64
# test:   make test      -> headless emulator run + screenshots + stdout scrape
ROMNAME := demo
BUILD_DIR := build

export N64_INST ?= $(HOME)/n64-sdk
# cross-gcc from pacman, not under SDK prefix
export N64_GCCPREFIX = /usr

ifeq ($(wildcard $(N64_INST)/include/n64.mk),)
  $(warning $(N64_INST)/include/n64.mk missing — run: make sdk first)
endif

all: $(ROMNAME).z64

# one-time: build+install libdragon into $(N64_INST)
sdk:
	$(MAKE) -C libdragon -j4 libdragon tools
	$(MAKE) -C libdragon install
	$(MAKE) -C libdragon tools-install

include $(N64_INST)/include/n64.mk

C_FILES := src/main.c
OBJS := $(addprefix $(BUILD_DIR)/,$(C_FILES:.c=.o))

$(BUILD_DIR)/$(ROMNAME).elf: $(OBJS)

$(ROMNAME).z64: N64_ROM_TITLE = "CHARGEBAY 64"

test: $(ROMNAME).z64
	bash tools/n64test.sh $(ROMNAME).z64 60,120,180

clean:
	$(RM) -r $(BUILD_DIR) *.z64
.PHONY: all test clean sdk
