/*
 * Minimal text command table for the MintPRINT graphics spike.
 * 0xFF means "no simple substitution" to printer.device.
 * Text/PRT: support comes later; for now DoSpecial() emits no bytes.
 */

#include <exec/types.h>

static char unsupported[] = "\377";

char *CommandTable[77] = {
    unsupported, unsupported, unsupported, unsupported, unsupported,
    unsupported, unsupported, unsupported, unsupported, unsupported,
    unsupported, unsupported, unsupported, unsupported, unsupported,
    unsupported, unsupported, unsupported, unsupported, unsupported,
    unsupported, unsupported, unsupported, unsupported, unsupported,
    unsupported, unsupported, unsupported, unsupported, unsupported,
    unsupported, unsupported, unsupported, unsupported, unsupported,
    unsupported, unsupported, unsupported, unsupported, unsupported,
    unsupported, unsupported, unsupported, unsupported, unsupported,
    unsupported, unsupported, unsupported, unsupported, unsupported,
    unsupported, unsupported, unsupported, unsupported, unsupported,
    unsupported, unsupported, unsupported, unsupported, unsupported,
    unsupported, unsupported, unsupported, unsupported, unsupported,
    unsupported, unsupported, unsupported, unsupported, unsupported,
    unsupported, unsupported, unsupported, unsupported, unsupported,
    unsupported, unsupported
};
