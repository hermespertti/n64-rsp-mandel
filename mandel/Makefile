ROMNAME := mandel
BUILD_DIR := build

export N64_INST ?= $(HOME)/n64-sdk
# cross-gcc from pacman, not under SDK prefix
export N64_GCCPREFIX = /usr

all: $(ROMNAME).z64

include $(N64_INST)/include/n64.mk

C_FILES := main.c
OBJS := $(addprefix $(BUILD_DIR)/,$(C_FILES:.c=.o))

$(BUILD_DIR)/$(ROMNAME).elf: $(OBJS)

$(ROMNAME).z64: N64_ROM_TITLE = "MANDEL RSP"

test: $(ROMNAME).z64
	bash ../tools/n64test_ares.sh $(ROMNAME).z64 25

clean:
	$(RM) -r $(BUILD_DIR) *.z64
.PHONY: all test clean
