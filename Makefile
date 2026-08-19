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

ART_DIR := art

# Stages a distributable bundle: MintPrintSettings copied next to a plain
# "MintPRINT" driver binary, matching PROGDIR:MintPRINT - the layout
# check_and_offer_driver_install() (src/MintPrintSettings.c) expects when
# it offers to install/update the driver from wherever MintPrintSettings
# itself is run from.
#
# Icons are copied from $(ART_DIR)/ if present there, matching AmigaOS
# icon placement: a drawer's icon lives in its PARENT directory (so
# MintPRINT.info lands next to $(RELEASE_DIR), not inside it), while an
# application's icon sits right next to its binary. The driver binary
# deliberately gets no icon at all - it is a printer.device driver
# segment, not a runnable program, and double-clicking it is unsafe.
release: gui driver
	mkdir -p $(RELEASE_DIR)
	cp MintPrintSettings $(RELEASE_DIR)/
	cp $(DRIVER_OUT) $(RELEASE_DIR)/MintPRINT
	cp Aminet/MintPRINT.readme release/MintPRINT.readme
	@if [ -f $(ART_DIR)/MintPrintSettings.info ]; then \
		cp $(ART_DIR)/MintPrintSettings.info $(RELEASE_DIR)/; \
		echo "Copied $(ART_DIR)/MintPrintSettings.info -> $(RELEASE_DIR)/"; \
	else \
		echo "No $(ART_DIR)/MintPrintSettings.info found - application will have no icon"; \
	fi
	@if [ -f $(ART_DIR)/MintPRINT.info ]; then \
		cp $(ART_DIR)/MintPRINT.info release/MintPRINT.info; \
		echo "Copied $(ART_DIR)/MintPRINT.info -> release/MintPRINT.info (drawer icon)"; \
	else \
		echo "No $(ART_DIR)/MintPRINT.info found - release drawer will have no icon"; \
	fi
	@echo
	@echo "Release bundle staged in $(RELEASE_DIR)/:"
	@echo "  MintPrintSettings       - run this to configure/install"
	@echo "  MintPRINT                - the driver it installs from PROGDIR:"
	@echo "                            (must stay next to MintPrintSettings;"
	@echo "                            deliberately has no icon)"
	@echo "  MintPrintSettings.info  - if $(ART_DIR)/ had one"
	@echo
	@echo "release/MintPRINT.info    - the drawer's own icon, if $(ART_DIR)/ had one"
	@echo "release/MintPRINT.readme  - the Aminet readme, staged next to the"
	@echo "drawer (not inside it) per Aminet convention: name it to match"
	@echo "whatever .lha/.zip archive you make of $(RELEASE_DIR)/."

clean:
	rm -rf build release MintPrintSettings
