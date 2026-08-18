CROSS   ?= m68k-amigaos-
CC       = $(CROSS)gcc
NM       = $(CROSS)nm
CFLAGS  ?= -Os -m68000 -Wall -Wextra -fomit-frame-pointer -fno-builtin

IFF_DIR := Archive/Old JPEG Decode
DRIVER_BUILD := build/driver
DRIVER_OUT := $(DRIVER_BUILD)/MintPRINT

.PHONY: all gui driver driver-symbols clean help

all: gui

help:
	@echo "MintPRINT targets:"
	@echo "  make gui      - build the existing MintPRINT setup/test GUI"
	@echo "  make driver   - build the experimental DEVS:Printers/MintPRINT driver"
	@echo "  make driver-symbols - show ABI symbols used by the driver"
	@echo "  make clean"

gui: MintPRINT

MintPRINT: src/IPP-Test16.c "$(IFF_DIR)/iff-loader.c" "$(IFF_DIR)/iff-loader.h"
	$(CC) -O2 -g -I"$(IFF_DIR)" -o $@ src/IPP-Test16.c "$(IFF_DIR)/iff-loader.c" -lamiga -lm

$(DRIVER_BUILD):
	mkdir -p $@

$(DRIVER_BUILD)/printertag.o: driver/printertag.s | $(DRIVER_BUILD)
	$(CC) -m68000 -c $< -o $@

$(DRIVER_BUILD)/driver_core.o: driver/driver_core.c | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/command_table.o: driver/command_table.c | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/jpeg_writer.o: driver/jpeg_writer.c driver/jpeg_writer.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/ipp_client.o: driver/ipp_client.c driver/ipp_client.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_OUT): $(DRIVER_BUILD)/printertag.o $(DRIVER_BUILD)/driver_core.o $(DRIVER_BUILD)/command_table.o $(DRIVER_BUILD)/jpeg_writer.o $(DRIVER_BUILD)/ipp_client.o
	$(CC) -m68000 -nostartfiles -Wl,-Map,$(DRIVER_BUILD)/MintPRINT.map \
		-o $@ $^ -lamiga

# The printer tag assembly expects classic Amiga leading-underscore C symbols.
# This target makes ABI mismatches obvious before installing anything on AmigaOS.
driver-symbols: $(DRIVER_BUILD)/driver_core.o $(DRIVER_BUILD)/command_table.o
	$(NM) $(DRIVER_BUILD)/driver_core.o | grep -E '(_Init|_Expunge|_DriverOpen|_DriverClose|_DoSpecial|_Render|_DriverTags|_PEDData)' || true
	$(NM) $(DRIVER_BUILD)/command_table.o | grep -E '_CommandTable' || true

driver: $(DRIVER_OUT)
	@echo
	@echo "Built experimental printer driver: $(DRIVER_OUT)"
	@echo "Read docs/PRINTER_DEVICE_SPIKE.md before installing it."

clean:
	rm -rf build MintPRINT
