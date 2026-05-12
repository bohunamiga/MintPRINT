#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <clib/gadtools_protos.h>
#include <clib/asl_protos.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <proto/dos.h>
#include <string.h>
#include <stdio.h>

#define GAD_IP      1
#define GAD_TEST    2
#define GAD_MEDIA   3
#define GAD_QUALITY 4
#define GAD_COLOUR  5
#define GAD_BW      6
#define GAD_DUPLEX  7
#define GAD_SELECT  8
#define GAD_PRINT   9

struct IntuitionBase *IntuitionBase;
struct Library *GadToolsBase;
struct Library *AslBase;

struct Window *win;
struct Gadget *gadlist = NULL;
struct Gadget *ip_strgadget, *test_btn, *media_cycle, *quality_cycle, *colour_chk, *bw_chk, *duplex_chk, *select_btn, *print_btn;
struct StringInfo ip_stringinfo;

UBYTE ipbuffer[64] = "192.168.0.44";
UBYTE *media_labels[] = { "A4", "Letter", NULL };
UBYTE *quality_labels[] = { "Normal", "Draft", NULL };
struct TextAttr Topaz80 = { (STRPTR)"topaz.font", 8, 0, 0 };

void cleanup() {
    if (win) {
        // Detach gadgets from the window before freeing
        if (gadlist) {
            win->FirstGadget = NULL;  // Detach gadgets from the window
            GT_RefreshWindow(win, NULL);  // Refresh the window to update the display
            FreeGadgets(gadlist);  // Free the gadget list
        }
        CloseWindow(win);
    }
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (AslBase) CloseLibrary(AslBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
}

int main() {
    struct Screen *scr;
    struct NewGadget ng;
    struct Gadget *last = NULL;
    struct VisualInfo *vi = NULL;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
    if (!IntuitionBase) {
        printf("Failed to open intuition.library\n");
        return 1;
    }

    GadToolsBase = OpenLibrary("gadtools.library", 37);
    AslBase = OpenLibrary("asl.library", 37);
    if (!GadToolsBase || !AslBase) {
        printf("Missing libraries\n");
        cleanup();
        return 1;
    }
    printf("GadTools version: %ld\n", GadToolsBase->lib_Version);

    scr = LockPubScreen(NULL);
    if (!scr) {
        printf("Unable to lock public screen\n");
        cleanup();
        return 1;
    }
    printf("Screen width: %d, height: %d, depth: %d\n", scr->Width, scr->Height, scr->RastPort.BitMap->Depth);

    // Open the window first, without gadgets
    win = OpenWindowTags(NULL,
        WA_Title, (ULONG)"IPP Printer GUI",
        WA_Width, 300,
        WA_Height, 300,
        WA_Flags, WFLG_SIZEGADGET | WFLG_CLOSEGADGET | WFLG_DRAGBAR,
        WA_IDCMP, IDCMP_GADGETUP | IDCMP_CLOSEWINDOW,
        TAG_END);

    if (!win) {
        printf("Failed to open window\n");
        UnlockPubScreen(NULL, scr);
        cleanup();
        return 1;
    }

    // Get VisualInfo for the screen
    vi = GetVisualInfo(scr, TAG_END);
    if (!vi) {
        printf("Failed to get visual info\n");
        UnlockPubScreen(NULL, scr);
        cleanup();
        return 1;
    }

    // Create the gadget context
    if (CreateContext(&gadlist) == NULL) {
        printf("Failed to create gadget context\n");
        FreeVisualInfo(vi);
        UnlockPubScreen(NULL, scr);
        cleanup();
        return 1;
    }

    // Initialize the NewGadget structure
    ng.ng_TextAttr = scr->Font;  // Use the screen's default font
    ng.ng_VisualInfo = vi;
    ng.ng_LeftEdge = 10; ng.ng_TopEdge = 10; ng.ng_Width = 200; ng.ng_Height = 20;
    ng.ng_GadgetText = "Printer IP:";
    ng.ng_GadgetID = GAD_IP;
    ng.ng_Flags = PLACETEXT_LEFT;

    // Initialize the StringInfo structure
    ip_stringinfo.Buffer = ipbuffer;
    ip_stringinfo.MaxChars = sizeof(ipbuffer);
    ip_stringinfo.BufferPos = 0;
    ip_stringinfo.NumChars = strlen(ipbuffer);

    // Create the IP string gadget
    ip_strgadget = CreateGadget(STRING_KIND, last, &ng, GTST_String, (ULONG)ipbuffer, GTST_MaxChars, sizeof(ipbuffer), TAG_DONE);
    if (!ip_strgadget) {
        printf("Failed to create IP string gadget (memory: %ld bytes free)\n", AvailMem(MEMF_CHIP));
        FreeVisualInfo(vi);
        UnlockPubScreen(NULL, scr);
        cleanup();
        return 1;
    }
    ip_strgadget->SpecialInfo = (APTR)&ip_stringinfo;  // Explicitly set the StringInfo
    last = ip_strgadget;

    ng.ng_TopEdge += 20; ng.ng_GadgetText = "Test and Update"; ng.ng_GadgetID = GAD_TEST;
    test_btn = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    if (!test_btn) {
        printf("Failed to create Test button\n");
        FreeVisualInfo(vi);
        UnlockPubScreen(NULL, scr);
        cleanup();
        return 1;
    }
    last = test_btn;

    ng.ng_TopEdge += 30; ng.ng_GadgetText = "Paper Size"; ng.ng_GadgetID = GAD_MEDIA;
    media_cycle = CreateGadget(CYCLE_KIND, last, &ng, GTCY_Labels, (ULONG)media_labels, TAG_DONE);
    if (!media_cycle) {
        printf("Failed to create Media cycle gadget\n");
        FreeVisualInfo(vi);
        UnlockPubScreen(NULL, scr);
        cleanup();
        return 1;
    }
    last = media_cycle;

    ng.ng_TopEdge += 20; ng.ng_GadgetText = "Quality"; ng.ng_GadgetID = GAD_QUALITY;
    quality_cycle = CreateGadget(CYCLE_KIND, last, &ng, GTCY_Labels, (ULONG)quality_labels, TAG_DONE);
    if (!quality_cycle) {
        printf("Failed to create Quality cycle gadget\n");
        FreeVisualInfo(vi);
        UnlockPubScreen(NULL, scr);
        cleanup();
        return 1;
    }
    last = quality_cycle;

    ng.ng_TopEdge += 20; ng.ng_GadgetText = "Colour"; ng.ng_GadgetID = GAD_COLOUR;
    colour_chk = CreateGadget(CHECKBOX_KIND, last, &ng, TAG_DONE);
    if (!colour_chk) {
        printf("Failed to create Colour checkbox\n");
        FreeVisualInfo(vi);
        UnlockPubScreen(NULL, scr);
        cleanup();
        return 1;
    }
    last = colour_chk;

    ng.ng_TopEdge += 20; ng.ng_GadgetText = "Black & White"; ng.ng_GadgetID = GAD_BW;
    bw_chk = CreateGadget(CHECKBOX_KIND, last, &ng, TAG_DONE);
    if (!bw_chk) {
        printf("Failed to create B&W checkbox\n");
        FreeVisualInfo(vi);
        UnlockPubScreen(NULL, scr);
        cleanup();
        return 1;
    }
    last = bw_chk;

    ng.ng_TopEdge += 20; ng.ng_GadgetText = "Duplex"; ng.ng_GadgetID = GAD_DUPLEX;
    duplex_chk = CreateGadget(CHECKBOX_KIND, last, &ng, TAG_DONE);
    if (!duplex_chk) {
        printf("Failed to create Duplex checkbox\n");
        FreeVisualInfo(vi);
        UnlockPubScreen(NULL, scr);
        cleanup();
        return 1;
    }
    last = duplex_chk;

    ng.ng_TopEdge += 30; ng.ng_GadgetText = "Select File"; ng.ng_GadgetID = GAD_SELECT;
    select_btn = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    if (!select_btn) {
        printf("Failed to create Select button\n");
        FreeVisualInfo(vi);
        UnlockPubScreen(NULL, scr);
        cleanup();
        return 1;
    }
    last = select_btn;

    ng.ng_TopEdge += 30; ng.ng_GadgetText = "PRINT SELECTED FILE"; ng.ng_GadgetID = GAD_PRINT;
    print_btn = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    if (!print_btn) {
        printf("Failed to create Print button\n");
        FreeVisualInfo(vi);
        UnlockPubScreen(NULL, scr);
        cleanup();
        return 1;
    }

    // Attach the gadgets to the window
    win->FirstGadget = gadlist;
    GT_RefreshWindow(win, NULL);

    BOOL running = TRUE;
    while (running) {
        ULONG sig = Wait(1L << win->UserPort->mp_SigBit);
        if (sig & (1L << win->UserPort->mp_SigBit)) {
            struct IntuiMessage *msg;
            while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
                if (msg->Class == IDCMP_CLOSEWINDOW) {
                    running = FALSE;
                } else if (msg->Class == IDCMP_GADGETUP) {
                    struct Gadget *gad = (struct Gadget *)msg->IAddress;
                    switch (gad->GadgetID) {
                        case GAD_TEST:
                            printf("Query printer attributes here\n");
                            break;
                        case GAD_SELECT:
                            printf("Open ASL file requester here\n");
                            break;
                        case GAD_PRINT:
                            printf("Send print job here\n");
                            break;
                    }
                }
                ReplyMsg((struct Message *)msg);
            }
        }
    }

    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, scr);
    cleanup();
    return 0;
}