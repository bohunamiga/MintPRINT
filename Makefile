# MintPRINT build system - AROS aarch64 is the default target (the AmigaOS
# incarnation that this repository is maintained on/flashed to), mirroring the
# structure the 2016-era AROS port fork used.  Building the classic m68k
# AmigaOS3.1/OS3.9 driver or the host-side unit tests is still supported via
# the overridable CROSS/HOSTCC variables (see help below), but none of those
# are part of the default invocation - `make` produces the AROS aarch64 GUI
# and driver, exactly like the fork did.

CROSS   ?= aarch64-aros-
CC      = $(CROSS)gcc
HOSTCC  ?= cc
PYTHON  ?= python3
NM      = $(CROSS)nm
CFLAGS  ?= -Os -Wall -Wextra -fomit-frame-pointer -fno-builtin

# --- AROS: hand the ELF compiler its SDK via --sysroot ---
# The crosstools ELF binaries carry a --sysroot baked in at build time that
# points at the original Linux build host's /work/... tree, which does not
# exist on any other machine.  The full SDK (include/ and lib/ with libaros.a)
# ships inside the same toolchain directory as the wrapper, so point the
# compiler there explicitly - a CLI --sysroot overrides the baked-in one and
# makes both headers and libaros resolve.  Only added when it truly exists
# (avoids guessing at tree names). */
AROS_GCC_PATH := $(shell command -v $(CC) 2>/dev/null)
AROS_SDK     := $(wildcard $(abspath $(dir $(AROS_GCC_PATH))/../sysroot))
ifneq ($(strip $(AROS_SDK)),)
CFLAGS += --sysroot="$(AROS_SDK)"
endif
AROS_SDK_LIB := $(wildcard $(AROS_SDK)/lib)
ifneq ($(strip $(AROS_SDK_LIB)),)
CFLAGS += -L"$(AROS_SDK_LIB)"
endif

IFF_DIR := Archive/Old JPEG Decode
IFF_DIR_ESC := Archive/Old\ JPEG\ Decode
DRIVER_BUILD := build/driver
DRIVER_OUT := $(DRIVER_BUILD)/MintPRINT
TEST_BUILD := build/tests
RELEASE_DIR := release/MintPRINT

.PHONY: all gui driver driver-symbols release clean help

all: gui driver

# --- AROS aarch64: jawny --sysroot ---
# The ELF cross-compiler carries a baked-in sysroot path that only exists on
# the machine that built the toolchain (an AROS source-tree build tree).  On
# any other host the SDK headers must be found explicitly, so point the
# compiler at the SDK's own aros-toolchain-aarch64/sysroot dir.  Matches the
# classic AmigaOS m68k behaviour of finding headers from the libnix/NDK.
ifeq (aarch64-aros-,$(CROSS))
SYSROOT := $(abspath $(dir $(shell command -v "$(CC)" 2>/dev/null))/../sysroot)
CFLAGS += $(if $(SYSROOT),--sysroot=$(SYSROOT))
endif

help:
	@echo "MintPRINT targets (AROS aarch64 default):"
	@echo "  make         - build the MintPRINT Settings GUI and the printer"
	@echo "                 driver (same as make all / make gui driver)"
	@echo "  make gui     - build MintPrintSettings (Settings/test GUI)"
	@echo "  make driver  - build the DEVS:Printers/MintPRINT printer driver"
	@echo "  make driver-symbols - show ABI symbols used by the driver"
	@echo "  make release - stage a distributable bundle under release/MintPRINT/"
	@echo "  make clean"
	@echo
	@echo "Cross-target overrides (classic AmigaOS m68k compatibility):"
	@echo "  make CROSS=m68k-amigaos- CFLAGS='-Os -m68000 -Wall -Wextra' gui driver" 

gui: MintPrintSettings

MintPrintSettings: src/MintPrintSettings.c src/http_response.c src/http_response.h \
		src/dpi_options.c src/dpi_options.h src/ipp_enum.c src/ipp_enum.h \
		src/mdns_endpoint.c src/mdns_endpoint.h src/lodepng.c src/lodepng.h \
		driver/media_size.c driver/media_size.h driver/ipp_client.c driver/ipp_client.h \
		$(IFF_DIR_ESC)/iff-loader.c $(IFF_DIR_ESC)/iff-loader.h
	$(CC) $(CFLAGS) -DLODEPNG_NO_COMPILE_ENCODER -DLODEPNG_NO_COMPILE_DISK \
		-DLODEPNG_NO_COMPILE_ANCILLARY_CHUNKS -DLODEPNG_NO_COMPILE_ERROR_TEXT \
		-I"$(IFF_DIR)" -Isrc -Idriver -o $@ \
		src/MintPrintSettings.c src/http_response.c src/dpi_options.c \
		src/ipp_enum.c src/mdns_endpoint.c src/lodepng.c \
		driver/media_size.c driver/ipp_client.c "$(IFF_DIR)/iff-loader.c" \
		-laros -lm

$(DRIVER_BUILD):
	mkdir -p $@

$(DRIVER_BUILD)/printertag_aros.o: driver/printertag_aros.c driver/printertag_aarch64.s | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/driver_core.o: driver/driver_core.c | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/command_table.o: driver/command_table.c | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/config.o: driver/config.c driver/config.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/media_size.o: driver/media_size.c driver/media_size.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/jpeg_writer.o: driver/jpeg_writer.c driver/jpeg_writer.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/pwg_writer.o: driver/pwg_writer.c driver/pwg_writer.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/pdf_writer.o: driver/pdf_writer.c driver/pdf_writer.h driver/jpeg_writer.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/postscript_writer.o: driver/postscript_writer.c driver/postscript_writer.h driver/jpeg_writer.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/urf_writer.o: driver/urf_writer.c driver/urf_writer.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_BUILD)/ipp_client.o: driver/ipp_client.c driver/ipp_client.h src/http_response.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(DRIVER_BUILD)/http_response.o: src/http_response.c src/http_response.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(DRIVER_BUILD)/spool.o: driver/spool.c driver/spool.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_OUT): $(DRIVER_BUILD)/printertag_aros.o $(DRIVER_BUILD)/driver_core.o \
		$(DRIVER_BUILD)/command_table.o $(DRIVER_BUILD)/config.o \
		$(DRIVER_BUILD)/media_size.o $(DRIVER_BUILD)/jpeg_writer.o \
		$(DRIVER_BUILD)/pwg_writer.o $(DRIVER_BUILD)/pdf_writer.o \
		$(DRIVER_BUILD)/postscript_writer.o $(DRIVER_BUILD)/urf_writer.o \
		$(DRIVER_BUILD)/ipp_client.o $(DRIVER_BUILD)/http_response.o $(DRIVER_BUILD)/spool.o
	$(CC) $(CFLAGS) -nostartfiles -Wl,-Map,$(DRIVER_BUILD)/MintPRINT.map \
		-o $@ $^ -laros

driver: $(DRIVER_OUT)
	@echo
	@echo "Built AROS aarch64 printer driver: $(DRIVER_OUT)"

driver-symbols: $(DRIVER_BUILD)/driver_core.o $(DRIVER_BUILD)/command_table.o
	$(NM) $(DRIVER_BUILD)/driver_core.o | grep -E '(_Init|_Expunge|_DriverOpen|_DriverClose|_DoSpecial|_Render|_DriverTags|_PEDData)' || true
	$(NM) $(DRIVER_BUILD)/command_table.o | grep -E '_CommandTable' || true

release: gui driver
	mkdir -p $(RELEASE_DIR)
	cp MintPrintSettings $(RELEASE_DIR)/
	cp $(DRIVER_OUT) $(RELEASE_DIR)/MintPRINT
	@echo
	@echo "Release bundle staged in $(RELEASE_DIR)/:"
	@echo "  MintPrintSettings         - per-job defaults + driver install helper"
	@echo "  MintPRINT                 - the DEVS:Printers driver (AROS aarch64)"

clean:
	rm -rf build release MintPrintSettings
