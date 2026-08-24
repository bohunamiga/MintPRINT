#include <exec/types.h>
#include <devices/prtbase.h>

extern LONG Init(void);
extern LONG Expunge(void);
extern LONG DriverOpen(void);
extern LONG DriverClose(void);
extern LONG DoSpecial(void);
extern LONG Render(void);
extern UBYTE CommandTable[];
extern struct TagItem DriverTags[];

static const char printerName[] = "MintPRINT";
const char mp_driver_revision_marker[] = "MPDRVREV:27";

/* Własna struktura wymuszająca format V44 (PrinterExtendedData) */
struct MintPrintSegment {
    ULONG  ps_runAlert;
    UWORD  ps_Version;
    UWORD  ps_Revision;
    struct PrinterExtendedData ps_PED;
};

/* Zmienna musi nazywać się PEDData, aby reszta kodu C mogła ją zlinkować! */
struct MintPrintSegment PEDData __attribute__((section(".text"))) = {
    0x00000000, 
    44,         
    1,          
    {
        (STRPTR)printerName,
        (void *)Init,
        (void *)Expunge,
        (void *)DriverOpen,
        (void *)DriverClose,
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
        (void *)DoSpecial,
        (void *)Render,
        30,
        NULL,
        0,
        NULL,
        DriverTags,
        NULL,
        NULL
    }
};
void __PROGRAM_ENTRIES__symbol_set_handler_missing(void) {}
