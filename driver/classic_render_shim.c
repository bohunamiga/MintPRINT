/*
 * MintPRINT AmigaOS 3.1 Render compatibility shim.
 *
 * The normal V44+ MintPRINT driver asks printer.device for PRTA_8BitGuns,
 * so each PrtInfo colorEntry contains 0..255 Y/M/C/B intensity values.
 *
 * AmigaOS 3.1's classic printer.device interface predates that tag.  Its
 * graphics pipeline uses the traditional 0..15 intensity range (the same
 * range consumed by the documented 4x4 printer dither matrix).
 *
 * Keep the actual MintPRINT renderer/encoders shared: the Makefile compiles
 * driver_core.c with:
 *
 *     -DRender=MintPRINT_RenderCore
 *
 * This file exports the real _Render entry point expected by the classic
 * PrinterExtendedData and, for raster-row calls only, supplies a temporary
 * PrtInfo whose color array has each 4-bit gun expanded to 8 bits:
 *
 *     0x0..0xF -> 0x00,0x11,...,0xFF
 *
 * No caller-owned printer.device memory is modified.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/printer.h>
#include <devices/prtbase.h>
#include <devices/prtgfx.h>
#include <proto/exec.h>

extern struct ExecBase *SysBase;

LONG PRT_STDARGS MintPRINT_RenderCore(LONG ct, LONG x, LONG y,
                                      LONG status, ...);

static union colorEntry *g_classic_colors = NULL;
static ULONG g_classic_colors_bytes = 0;

static void mp_classic_free_colors(void)
{
    if (g_classic_colors) {
        FreeMem(g_classic_colors, g_classic_colors_bytes);
        g_classic_colors = NULL;
        g_classic_colors_bytes = 0;
    }
}

static UBYTE mp_expand_nibble(UBYTE value)
{
    /*
     * On the intended V40 path value is 0..15.  The >15 guard makes the
     * shim harmless if a replacement printer.device already supplies 8-bit
     * values despite using the classic PED ABI.
     */
    if (value <= 15)
        return (UBYTE)((value << 4) | value);
    return value;
}

static BOOL mp_classic_prepare_row(struct PrtInfo *src,
                                   struct PrtInfo *dst)
{
    ULONG needed;
    ULONG i;

    if (!src || !dst || !src->pi_ColorInt || src->pi_width == 0)
        return FALSE;

    needed = (ULONG)src->pi_width * (ULONG)sizeof(union colorEntry);

    if (needed > g_classic_colors_bytes) {
        union colorEntry *new_colors;

        new_colors = (union colorEntry *)AllocMem(needed, MEMF_PUBLIC);
        if (!new_colors)
            return FALSE;

        mp_classic_free_colors();
        g_classic_colors = new_colors;
        g_classic_colors_bytes = needed;
    }

    /* Shallow-copy PrtInfo; only pi_ColorInt is replaced below. */
    *dst = *src;
    dst->pi_ColorInt = g_classic_colors;

    for (i = 0; i < (ULONG)src->pi_width; ++i) {
        union colorEntry *in = &src->pi_ColorInt[i];
        union colorEntry *out = &g_classic_colors[i];

        *out = *in;
        out->colorByte[PCMYELLOW]  = mp_expand_nibble(in->colorByte[PCMYELLOW]);
        out->colorByte[PCMMAGENTA] = mp_expand_nibble(in->colorByte[PCMMAGENTA]);
        out->colorByte[PCMCYAN]    = mp_expand_nibble(in->colorByte[PCMCYAN]);
        out->colorByte[PCMBLACK]   = mp_expand_nibble(in->colorByte[PCMBLACK]);
    }

    return TRUE;
}

LONG PRT_STDARGS Render(LONG ct, LONG x, LONG y, LONG status, ...)
{
    LONG result;

    if (status == 1) {
        struct PrtInfo *src = (struct PrtInfo *)ct;
        struct PrtInfo classic_row;

        if (!mp_classic_prepare_row(src, &classic_row))
            return PDERR_BUFFERMEMORY;

        return MintPRINT_RenderCore((LONG)&classic_row, x, y, status);
    }

    result = MintPRINT_RenderCore(ct, x, y, status);

    /* Case 4 is the documented graphics-dump close-down call. */
    if (status == 4)
        mp_classic_free_colors();

    return result;
}
