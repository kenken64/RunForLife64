PROG_NAME = runforlife64
SOURCE_DIR = src
BUILD_DIR = build
PYTHON ?= C:/Python314/python.exe
BLENDER ?= $(CURDIR)/.tools/blender/blender-4.5.8-windows-x64/blender.exe

GENERATED_MODEL_SOURCE = $(SOURCE_DIR)/building_models.c
GENERATED_MODEL_HEADER = $(SOURCE_DIR)/building_models.h
SOURCES = $(filter-out $(GENERATED_MODEL_SOURCE),$(wildcard $(SOURCE_DIR)/*.c)) $(GENERATED_MODEL_SOURCE)
OBJS = $(patsubst $(SOURCE_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
MODEL_GLBS = $(wildcard assets/models/building*.glb) assets/models/backtofuture.glb

N64_INST ?= $(CURDIR)/.tools/libdragon-install
N64_GCCPREFIX ?= $(CURDIR)/.tools/n64-toolchain
DEBUG_IPL3 ?= $(CURDIR)/.tools/libdragon/boot/bin/ipl3_dev.z64
PROJECT64_ROM_REGION = E
PROJECT64_ROM_PAYLOAD_SIZE = 1020K

ifeq ($(wildcard $(N64_INST)/include/n64.mk),)
$(error libdragon SDK not found at $(N64_INST). Install libdragon or set N64_INST)
endif

export PATH := $(N64_INST)/bin:$(N64_GCCPREFIX)/bin:$(PATH)
export N64_INST
export N64_GCCPREFIX

include $(N64_INST)/include/n64.mk

# Project64 is stricter than libdragon/flashcarts about small homebrew ROMs.
# n64tool's --size excludes the 4 KiB IPL3 header, so 1020 KiB makes a 1 MiB ROM.
N64_TOOLFLAGS += --region $(PROJECT64_ROM_REGION) --size $(PROJECT64_ROM_PAYLOAD_SIZE)

CFLAGS += -std=gnu11 -Wall -Wextra -I$(SOURCE_DIR) -falign-functions=32
N64_CFLAGS += -Wno-error=ignored-qualifiers -Wno-error=type-limits -Wno-error=unused-parameter

all: $(PROG_NAME).z64
.PHONY: all

debug-rom:
	$(MAKE) PROG_NAME=$(PROG_NAME)-debug N64_ROM_HEADER=$(DEBUG_IPL3) N64_ROM_ELFCOMPRESS=0 $(PROG_NAME)-debug.z64
.PHONY: debug-rom

models: $(GENERATED_MODEL_SOURCE)
.PHONY: models

$(GENERATED_MODEL_SOURCE) $(GENERATED_MODEL_HEADER): $(MODEL_GLBS) tools/glb_to_rfl_mesh.py
	$(PYTHON) tools/glb_to_rfl_mesh.py

$(PROG_NAME).z64: N64_ROM_TITLE = "Run For Life 64"
$(PROG_NAME).z64: $(BUILD_DIR)/$(PROG_NAME).dfs

$(BUILD_DIR)/$(PROG_NAME).elf: $(OBJS)

$(BUILD_DIR)/render.o: $(GENERATED_MODEL_HEADER)
$(BUILD_DIR)/building_models.o: $(GENERATED_MODEL_SOURCE) $(GENERATED_MODEL_HEADER)

$(BUILD_DIR)/%.o: $(SOURCE_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(GENERATED_MODEL_SOURCE) $(GENERATED_MODEL_HEADER) *.z64
.PHONY: clean

-include $(wildcard $(BUILD_DIR)/*.d)
