CROSS       ?= aarch64-aros-
CC          = $(CROSS)gcc
CC_GUI      ?= $(CC)
LDFLAGS_GUI ?= -laros -lm
HOSTCC      ?= cc
NM          = $(CROSS)nm
CFLAGS      ?= -Os -Wall -Wextra -fomit-frame-pointer -fno-builtin

IFF_DIR := Archive/Old JPEG Decode
IFF_DIR_ESC := Archive/Old\ JPEG\ Decode
DRIVER_BUILD := build/driver
DRIVER_OUT := $(DRIVER_BUILD)/MintPRINT
TEST_BUILD := build/tests
RELEASE_DIR := release/MintPRINT

.PHONY: all gui driver release clean

all: release

gui: MintPrintSettings

MintPrintSettings: src/MintPrintSettings.c src/http_response.c src/http_response.h src/dpi_options.c src/dpi_options.h src/ipp_enum.c src/ipp_enum.h driver/media_size.c driver/media_size.h $(IFF_DIR_ESC)/iff-loader.c $(IFF_DIR_ESC)/iff-loader.h
	$(CC_GUI) -O2 -s -I"$(IFF_DIR)" -Isrc -Idriver -o $@ src/MintPrintSettings.c src/http_response.c src/dpi_options.c src/ipp_enum.c driver/media_size.c "$(IFF_DIR)/iff-loader.c" $(LDFLAGS_GUI)
	@echo "=== MintPrintSettings gotowy ==="
	@file $@ || true

$(DRIVER_BUILD):
	mkdir -p $@

$(DRIVER_BUILD)/printertag_aros.o: driver/printertag_aros.c | $(DRIVER_BUILD)
	$(CC) -c $< -o $@

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

$(DRIVER_BUILD)/ipp_client.o: driver/ipp_client.c driver/ipp_client.h src/http_response.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(DRIVER_BUILD)/http_response.o: src/http_response.c src/http_response.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(DRIVER_BUILD)/spool.o: driver/spool.c driver/spool.h | $(DRIVER_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRIVER_OUT): $(DRIVER_BUILD)/printertag_aros.o $(DRIVER_BUILD)/driver_core.o $(DRIVER_BUILD)/command_table.o $(DRIVER_BUILD)/config.o $(DRIVER_BUILD)/media_size.o $(DRIVER_BUILD)/jpeg_writer.o $(DRIVER_BUILD)/pwg_writer.o $(DRIVER_BUILD)/pdf_writer.o $(DRIVER_BUILD)/postscript_writer.o $(DRIVER_BUILD)/ipp_client.o $(DRIVER_BUILD)/http_response.o $(DRIVER_BUILD)/spool.o
	$(CC) -nostartfiles -Wl,-Map,$(DRIVER_BUILD)/MintPRINT.map -o $@ $^ -laros

driver: $(DRIVER_OUT)

release: gui driver
	mkdir -p $(RELEASE_DIR)
	cp MintPrintSettings $(RELEASE_DIR)/
	cp $(DRIVER_OUT) $(RELEASE_DIR)/MintPRINT
	@echo "=== Pakiet release gotowy w $(RELEASE_DIR) ==="

clean:
	rm -rf build release MintPrintSettings MintPrintSettings-aros
