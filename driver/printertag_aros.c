#include <exec/types.h>
#include <devices/prtbase.h>

extern LONG Init(struct PrinterData *pd);
extern VOID Expunge(void);
extern int TextDriverOpen(struct IORequest *ior);
extern VOID TextDriverClose(struct IORequest *ior);
extern LONG TextDoSpecial(UWORD *command, UBYTE output_buffer[],
                          BYTE *current_line_position,
                          BYTE *current_line_spacing,
                          BYTE *crlf_flag, STRPTR params);
extern LONG MintPRINTCompatRender(LONG ct, LONG x, LONG y, LONG status, ...);
extern LONG ConvFunc(UBYTE *buf, UBYTE c, LONG crlf_flag);
extern UBYTE CommandTable[];
extern struct TagItem DriverTags[];

static const char printerName[] = "MintPRINT";

/* Same build-identity marker convention as printertag_aarch64.s, kept in
 * step with printertag.s's $VER: string and driver_core.c's revision
 * counter. MintPrint Settings reads it for driver update detection. */
const char mp_driver_version_marker[] = "$VER: MintPRINT 41.18 (03.09.2026)";

/* ABI marker for diagnostics */
const char mp_driver_abi_marker[] = "MPDRVABI:AROSaarch64";

/* Wlasna struktura wymuszajaca format V44 (PrinterExtendedData) */
struct MintPrintSegment {
    ULONG  ps_runAlert;
    UWORD  ps_Version;
    UWORD  ps_Revision;
    struct PrinterExtendedData ps_PED;
};

/* Zmienna musi nazywac sie PEDData, aby reszta kodu C mogla ja zlinkowac! */
struct MintPrintSegment PEDData __attribute__((section(".text"))) = {
    0x00000000,
    44,
    1,
    {
        (STRPTR)printerName,
        (void *)Init,
        (void *)Expunge,
        (void *)TextDriverOpen,
        (void *)TextDriverClose,
        PPC_COLORGFX | PPCF_EXTENDED,
        PCC_YMCB,
        136,
        0,
        1,
        4096,
        6144,
        300,
        300,
        (STRPTR *)CommandTable,
        (void *)TextDoSpecial,
        (void *)MintPRINTCompatRender,
        30,
        NULL,
        0,
        (void *)ConvFunc,
        DriverTags,
        NULL,
        NULL
    }
};
void __PROGRAM_ENTRIES__symbol_set_handler_missing(void) {}