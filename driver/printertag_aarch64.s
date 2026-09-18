/*
 * MintPRINT printer.device driver segment tag - AROS aarch64.
 *
 * Mirrors printertag.s but uses aarch64 instruction encoding and 8-byte
 * (.quad) pointer fields, matching the native AROS aarch64 ABI where APTR
 * is 64 bits. The PED symbol list is kept in step with printertag.s
 * (_TextDriverOpen/_TextDriverClose/_TextDoSpecial/_ConvFunc and the
 * _MintPRINTCompatRender width-stabilised Render hook).
 *
 * Frontier data: the working AROS target links the C equivalent
 * (printertag_aros.c) through the AROS SDK's own struct
 * PrinterExtendedData, which guarantees matching alignment/padding.
 * This file documents the exact in-memory layout for the aarch64 ABI and
 * provides an assembly build route if one is ever needed:
 *
 *   Offset 0  : ped_PrinterName  (APTR, 8 bytes)
 *   Offset 8  : ped_Init         (APTR, 8 bytes)
 *   Offset 16 : ped_Expunge      (APTR, 8 bytes)
 *   Offset 24 : ped_Open         (APTR, 8 bytes)
 *   Offset 32 : ped_Close        (APTR, 8 bytes)
 *   Offset 40 : ped_PrinterClass (UBYTE), ped_ColorClass (UBYTE),
 *               ped_MaxColumns (UBYTE), ped_NumCharSets (UBYTE)
 *   Offset 44 : ped_NumRows      (UWORD, 2 bytes)
 *   Offset 46 : [2 bytes padding to align ULONG to 4 bytes]
 *   Offset 48 : ped_MaxXDots     (ULONG, 4 bytes)
 *   Offset 52 : ped_MaxYDots     (ULONG, 4 bytes)
 *   Offset 56 : ped_XDotsInch    (UWORD, 2 bytes)
 *   Offset 58 : ped_YDotsInch    (UWORD, 2 bytes)
 *   Offset 60 : [4 bytes padding to align APTR to 8 bytes]
 *   Offset 64 : ped_CommandTable (APTR, 8 bytes)
 *   Offset 72 : ped_DoSpecial    (APTR, 8 bytes)
 *   Offset 80 : ped_Render       (APTR, 8 bytes)
 *   Offset 88 : ped_TimeoutSecs  (LONG, 4 bytes)
 *   Offset 92 : [4 bytes padding to align APTR to 8 bytes]
 *   Offset 96 : ped_8BitChars    (APTR, 8 bytes)
 *   Offset 104: ped_PrintMode    (LONG, 4 bytes)
 *   Offset 108: [4 bytes padding to align APTR to 8 bytes]
 *   Offset 112: ped_ConvFunc     (APTR, 8 bytes)
 *   Offset 120: ped_DriverTags   (APTR, 8 bytes) - V44 extension
 *   Offset 128: ped_DoPreferences(APTR, 8 bytes) - V44 extension
 *   Offset 136: ped_CallErrHook  (APTR, 8 bytes) - V44 extension
 */

        .section .text
        .balign 8
        .globl  _start
        .globl  _PEDData

        .extern _Init
        .extern _Expunge
        .extern _TextDriverOpen
        .extern _TextDriverClose
        .extern _CommandTable
        .extern _TextDoSpecial
        .extern _MintPRINTCompatRender
        .extern _ConvFunc
        .extern _DriverTags

_start:
        /*
         * ps_runAlert equivalent for aarch64.
         *
         * If a user double-clicks this driver from Workbench, these two
         * aarch64 instructions safely return 0 without crashing:
         *   MOV  W0, #0   little-endian encoding: 0x52 0x80 0x00 0x00
         *   RET           little-endian encoding: 0xD6 0x5F 0x03 0xC0
         */
        .long   0x52800000          /* MOV W0, #0 */
        .long   0xD65F03C0          /* RET        */

        /* PrinterSegment version/revision. Version 44 = V44 extended PED. */
        .short  44
        .short  1

        /* Align PEDData to 8 bytes (the natural alignment of the first
         * APTR field on aarch64). */
        .balign 8

_PEDData:
        /* ped_PrinterName */
        .quad   printerName

        /* ped_Init / ped_Expunge / ped_Open / ped_Close */
        .quad   _Init
        .quad   _Expunge
        .quad   _TextDriverOpen
        .quad   _TextDriverClose

        /* ped_PrinterClass = PPC_COLORGFX | PPCF_EXTENDED (0x07) */
        .byte   0x07
        /* ped_ColorClass = PCC_YMCB (0x04) */
        .byte   0x04
        /* ped_MaxColumns */
        .byte   136
        /* ped_NumCharSets */
        .byte   0
        /* ped_NumRows */
        .short  1
        /* 2 bytes padding - align ped_MaxXDots (ULONG) to 4 bytes */
        .short  0

        /* ped_MaxXDots / ped_MaxYDots */
        .long   4096
        .long   6144

        /* ped_XDotsInch / ped_YDotsInch */
        .short  300
        .short  300
        /* 4 bytes padding - align ped_CommandTable (APTR) to 8 bytes */
        .long   0

        /* ped_CommandTable / ped_DoSpecial / ped_Render
         * (Render uses the width-stabilising MintPRINTCompatRender hook) */
        .quad   _CommandTable
        .quad   _TextDoSpecial
        .quad   _MintPRINTCompatRender

        /* ped_TimeoutSecs */
        .long   30
        /* 4 bytes padding - align ped_8BitChars (APTR) to 8 bytes */
        .long   0

        /* ped_8BitChars: use system default */
        .quad   0

        /* ped_PrintMode */
        .long   0
        /* 4 bytes padding - align ped_ConvFunc (APTR) to 8 bytes */
        .long   0

        /* ped_ConvFunc: capture PRT:/CMD_WRITE characters */
        .quad   _ConvFunc

        /* V44 extended fields */
        .quad   _DriverTags
        .quad   0                   /* ped_DoPreferences */
        .quad   0                   /* ped_CallErrHook */

printerName:
        .asciz  "MintPRINT"
        .balign 4

/*
 * This project's own driver build version, kept in step with the $VER:
 * string in printertag.s (and driver_core.c's MP_DRIVER_REV/SUBREV), so
 * MintPrint Settings' update-detection and AmigaOS's "Version" command
 * report the same build identity on every mirror.
 */
mp_driver_version_marker:
        .asciz  "$VER: MintPRINT 41.18 (03.09.2026)"
        .balign 4

/* ABI marker for diagnostics */
mp_driver_abi_marker:
        .asciz  "MPDRVABI:AROSaarch64"
        .balign 4