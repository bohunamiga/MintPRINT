CROSS   ?= m68k-amigaos-
CC       = $(CROSS)gcc
NM       = $(CROSS)nm
CFLAGS  ?= -Os -m68000 -Wall -Wextra -fomit-frame-pointer -fno-builtin

IFF_DIR := Archive/Old JPEG Decode
IFF_DIR_ESC := Archive/Old\ JPEG\ Decode
DRIVER_BUILD := build/driver
DRIVER_OUT := $(DRIVER_BUILD)/MintPRINT
RELEASE_DIR := release/MintPRINT

.PHONY: all gui driver driver-symbols release clean help

all: gui

help:
	@echo "MintPRINT targets:"
	@echo "  make gui      - build MintPrint Settings (setup/test GUI)"
	@echo "  make driver   - build the experimental DEVS:Printers/MintPRINT driver"
	@echo "  make driver-symbols - show ABI symbols used by the driver"
	@echo "  make release  - build both and stage a distributable bundle"
	@echo "  make clean"

gui: MintPrintSettings

MintPrintSettings: src/MintPrintSettings.c $(IFF_DIR_ESC)/iff-loader.c $(IFF_DIR_ESC)/iff-loader.h
	$(CC) -O2 -g -I"$(IFF_DIR)" -o $@ src/MintPrintSettings.c "$(IFF_DIR)/iff-loader.c" -lamiga -lm

$(DRIVER_BUILD):
	mkdir -p $@

$(DRIVER_BUILD)/printertag.o: driver/printertag.s | $(DRIVER_BUILD)
	$(CC) -m68000 -c $< -o $@

$(DRIVER_BUILD)/driver_core.o: driver/driver_core.c | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/command_table.o: driver/command_table.c | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/config.o: driver/config.c driver/config.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/jpeg_writer.o: driver/jpeg_writer.c driver/jpeg_writer.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/pwg_writer.o: driver/pwg_writer.c driver/pwg_writer.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/pdf_writer.o: driver/pdf_writer.c driver/pdf_writer.h driver/jpeg_writer.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/ipp_client.o: driver/ipp_client.c driver/ipp_client.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/spool.o: driver/spool.c driver/spool.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_OUT): $(DRIVER_BUILD)/printertag.o $(DRIVER_BUILD)/driver_core.o $(DRIVER_BUILD)/command_table.o $(DRIVER_BUILD)/config.o $(DRIVER_BUILD)/jpeg_writer.o $(DRIVER_BUILD)/pwg_writer.o $(DRIVER_BUILD)/pdf_writer.o $(DRIVER_BUILD)/ipp_client.o $(DRIVER_BUILD)/spool.o
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

# Stages a distributable bundle: MintPrintSettings copied next to a plain
# "MintPRINT" driver binary, matching PROGDIR:MintPRINT - the layout
# check_and_offer_driver_install() (src/MintPrintSettings.c) expects when
# it offers to install/update the driver from wherever MintPrintSettings
# itself is run from. Does not generate icons - add MintPrintSettings.info
# and MintPRINT.info inside $(RELEASE_DIR), and a drawer icon named to
# match this folder in its parent directory, before distributing.
release: gui driver
	mkdir -p $(RELEASE_DIR)
	cp MintPrintSettings $(RELEASE_DIR)/
	cp $(DRIVER_OUT) $(RELEASE_DIR)/MintPRINT
	@echo
	@echo "Release bundle staged in $(RELEASE_DIR)/:"
	@echo "  MintPrintSettings  - run this to configure/install"
	@echo "  MintPRINT          - the driver it installs from PROGDIR: (must"
	@echo "                       stay next to MintPrintSettings)"
	@echo
	@echo "No icons generated - add MintPrintSettings.info and MintPRINT.info"
	@echo "inside $(RELEASE_DIR)/, and a drawer icon matching this folder's"
	@echo "name in its parent directory, before distributing."

clean:
	rm -rf build release MintPrintSettings
