/*
 * MintPRINT printer.device driver segment tag.
 *
 * The AmigaDOS loader supplies ps_NextSegment before the bytes below.
 * The first four bytes here are ps_runAlert (MOVEQ #0,D0 / RTS), followed
 * by the printer segment version/revision and PrinterExtendedData.
 *
 * Numeric constants used here are from devices/prtbase.h:
 *   PPC_COLORGFX  = 0x03
 *   PPCF_EXTENDED = 0x04
 *   PCC_YMCB      = 0x04
 */

        .section .text
        .even
        .globl  _start
        .globl  _PEDData

        .extern _Init
        .extern _Expunge
        .extern _DriverOpen
        .extern _DriverClose
        .extern _CommandTable
        .extern _DoSpecial
        .extern _Render
        .extern _DriverTags

_start:
        /* MOVEQ #0,D0 ; RTS -- safe if somebody tries to execute driver */
        .byte   0x70,0x00,0x4e,0x75

        /* PrinterSegment version/revision. V44 enables extended PED tags. */
        .word   44
        .word   1

_PEDData:
        .long   printerName
        .long   _Init
        .long   _Expunge
        .long   _DriverOpen
        .long   _DriverClose

        /* PrinterClass = PPC_COLORGFX | PPCF_EXTENDED */
        .byte   0x07
        .byte   0x04              /* ColorClass = PCC_YMCB */
        .byte   136               /* MaxColumns */
        .byte   0                 /* NumCharSets */
        .word   1                 /* NumRows: one raster row per cycle */
        .long   4096              /* MaxXDots */
        .long   6144              /* MaxYDots */
        .word   300               /* XDotsInch */
        .word   300               /* YDotsInch */
        .long   _CommandTable
        .long   _DoSpecial
        .long   _Render
        .long   30                /* timeout seconds */
        .long   0                 /* ped_8BitChars: use system default */
        .long   0                 /* ped_PrintMode */
        .long   0                 /* ped_ConvFunc */

        /* V44 extended fields */
        .long   _DriverTags
        .long   0                 /* ped_DoPreferences */
        .long   0                 /* ped_CallErrHook */

printerName:
        .asciz  "MintPRINT"
        .even
