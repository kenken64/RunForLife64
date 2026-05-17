PROG_NAME = runforlife64
SOURCE_DIR = src
BUILD_DIR = build
PYTHON ?= C:/Python314/python.exe
BLENDER ?= $(CURDIR)/.tools/blender/blender-4.5.8-windows-x64/blender.exe
T3D_CONVERTER ?= $(CURDIR)/.tools/tiny3d/tools/gltf_importer/gltf_to_t3d.exe
HOST_UCRT_PATH = PATH=$(CURDIR)/.tools/msys64/ucrt64/bin:$$PATH

GENERATED_MODEL_SOURCE = $(SOURCE_DIR)/building_models.c
GENERATED_MODEL_HEADER = $(SOURCE_DIR)/building_models.h
SOURCES = $(filter-out $(GENERATED_MODEL_SOURCE),$(wildcard $(SOURCE_DIR)/*.c)) $(GENERATED_MODEL_SOURCE)
OBJS = $(patsubst $(SOURCE_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
MODEL_GLBS = $(wildcard assets/models/building*.glb) assets/models/backtofuture.glb
SCRAPPER_BLEND_DEPS = assets/meshes/characters/scrapper_kid.blend \
	assets/materials/characters/player_hair.mat.blend \
	assets/materials/characters/player_leather.mat.blend \
	assets/materials/characters/player_red_cloth.mat.blend \
	assets/materials/characters/player_skin.mat.blend \
	assets/materials/characters/player_suit+tools.mat.blend \
	assets/materials/metal/metal_reflect.mat.blend \
	assets/meshes/vehicles/bike.blend
T3D_RUNTIME_ASSETS = filesystem/meshes/characters/scrapper_kid.t3dm filesystem/models/building2.t3dm

N64_INST ?= $(CURDIR)/.tools/libdragon-install
N64_GCCPREFIX ?= $(CURDIR)/.tools/n64-toolchain
PROJECT64_ROM_REGION = E
PROJECT64_ROM_PAYLOAD_SIZE = 1020K

ifeq ($(wildcard $(N64_INST)/include/n64.mk),)
$(error libdragon SDK not found at $(N64_INST). Install libdragon or set N64_INST)
endif

export PATH := $(N64_INST)/bin:$(N64_GCCPREFIX)/bin:$(PATH)
export N64_INST
export N64_GCCPREFIX

include $(N64_INST)/include/n64.mk
include $(N64_INST)/include/t3d.mk

# Project64 is stricter than libdragon/flashcarts about small homebrew ROMs.
# n64tool's --size excludes the 4 KiB IPL3 header, so 1020 KiB makes a 1 MiB ROM.
N64_TOOLFLAGS += --region $(PROJECT64_ROM_REGION) --size $(PROJECT64_ROM_PAYLOAD_SIZE)

CFLAGS += -std=gnu11 -Wall -Wextra -I$(SOURCE_DIR) -falign-functions=32
N64_CFLAGS += -Wno-error=ignored-qualifiers -Wno-error=type-limits -Wno-error=unused-parameter

all: $(PROG_NAME).z64
.PHONY: all

models: $(GENERATED_MODEL_SOURCE)
.PHONY: models

tiny-assets: $(T3D_RUNTIME_ASSETS)
.PHONY: tiny-assets

$(GENERATED_MODEL_SOURCE) $(GENERATED_MODEL_HEADER): $(MODEL_GLBS) tools/glb_to_rfl_mesh.py
	$(PYTHON) tools/glb_to_rfl_mesh.py

$(T3D_CONVERTER):
	$(HOST_UCRT_PATH) $(MAKE) -C .tools/tiny3d/tools/gltf_importer

$(BUILD_DIR)/assets/scrapper_kid.glb: $(SCRAPPER_BLEND_DEPS) tools/blend_export_glb.py
	@mkdir -p $(dir $@)
	$(BLENDER) --background assets/meshes/characters/scrapper_kid.blend --python tools/blend_export_glb.py -- $@

filesystem/meshes/characters/scrapper_kid.t3dm: $(BUILD_DIR)/assets/scrapper_kid.glb $(T3D_CONVERTER)
	@mkdir -p $(dir $@)
	$(HOST_UCRT_PATH) $(T3D_CONVERTER) $< $@ --ignore-materials --base-scale=64

$(BUILD_DIR)/assets/building2.glb: assets/models/building2.blend tools/blend_export_glb.py
	@mkdir -p $(dir $@)
	$(BLENDER) --background $< --python tools/blend_export_glb.py -- $@ 320

filesystem/models/building2.t3dm: $(BUILD_DIR)/assets/building2.glb $(T3D_CONVERTER)
	@mkdir -p $(dir $@)
	$(HOST_UCRT_PATH) $(T3D_CONVERTER) $< $@ --ignore-materials --base-scale=16

$(PROG_NAME).z64: N64_ROM_TITLE = "Run For Life 64"
$(PROG_NAME).z64: $(BUILD_DIR)/$(PROG_NAME).dfs
$(BUILD_DIR)/$(PROG_NAME).dfs: $(T3D_RUNTIME_ASSETS)

$(BUILD_DIR)/$(PROG_NAME).elf: $(OBJS)

$(BUILD_DIR)/render.o: $(GENERATED_MODEL_HEADER)
$(BUILD_DIR)/building_models.o: $(GENERATED_MODEL_SOURCE) $(GENERATED_MODEL_HEADER)

$(BUILD_DIR)/%.o: $(SOURCE_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(GENERATED_MODEL_SOURCE) $(GENERATED_MODEL_HEADER) $(T3D_RUNTIME_ASSETS) *.z64
.PHONY: clean

-include $(wildcard $(BUILD_DIR)/*.d)
