#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <stdio.h>

struct Library *IntuitionBase = NULL;
struct Library *GadToolsBase = NULL;

int main(void) {
    IntuitionBase = OpenLibrary("intuition.library", 37);
    if (!IntuitionBase) {
        printf("Failed to open intuition.library\n");
        return 1;
    }

    GadToolsBase = OpenLibrary("gadtools.library", 37);
    if (!GadToolsBase) {
        printf("Failed to open gadtools.library\n");
        CloseLibrary(IntuitionBase);
        return 1;
    }
    printf("GadTools library version: %ld\n", GadToolsBase->lib_Version);

struct Screen *screen = OpenScreenTags(NULL,
    SA_Width, 640,
    SA_Height, 200,
    SA_Depth, 2,
    SA_DisplayID, LORES_KEY,
    SA_ShowTitle, FALSE,
    TAG_DONE);
if (!screen) {
    printf("Could not open custom screen\n");
    CloseLibrary(GadToolsBase);
    CloseLibrary(IntuitionBase);
    return 1;
}

void *vi = GetVisualInfo(screen, TAG_DONE);
if (!vi) {
    printf("Failed to get visual info\n");
    CloseScreen(screen);
    CloseLibrary(GadToolsBase);
    CloseLibrary(IntuitionBase);
    return 1;
}

struct Window *window = OpenWindowTags(NULL,
    WA_Title, (ULONG)"String Gadget Test",
    WA_Width, 300,
    WA_Height, 100,
    WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR,
    WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP,
    WA_CustomScreen, (ULONG)screen,
    TAG_DONE);
    if (!window) {
        printf("Failed to open window\n");
        FreeVisualInfo(vi);
        UnlockPubScreen(NULL, screen);
        CloseLibrary(GadToolsBase);
        CloseLibrary(IntuitionBase);
        return 1;
    }

    struct Gadget *glist = NULL;
    if (CreateContext(&glist) == NULL) {
        printf("Failed to create gadget context\n");
        FreeVisualInfo(vi);
        CloseWindow(window);
        UnlockPubScreen(NULL, screen);
        CloseLibrary(GadToolsBase);
        CloseLibrary(IntuitionBase);
        return 1;
    }

    struct NewGadget ng;
    ng.ng_TextAttr = screen->Font;
    ng.ng_VisualInfo = vi;
    ng.ng_LeftEdge = 10;
    ng.ng_TopEdge = 20;
    ng.ng_Width = 200;
    ng.ng_Height = 15;
    ng.ng_GadgetText = (STRPTR)"Test String:";
    ng.ng_GadgetID = 1;
    ng.ng_Flags = PLACETEXT_LEFT;

    char buffer[64] = "Test";
    struct Gadget *str_gadget = CreateGadget(STRING_KIND, NULL, &ng,
        GTST_String, (ULONG)buffer,
        GTST_MaxChars, sizeof(buffer) - 1,
        TAG_DONE);
    if (!str_gadget) {
        printf("Failed to create string gadget (memory: %ld bytes free)\n", AvailMem(MEMF_CHIP));
        FreeGadgets(glist);
        FreeVisualInfo(vi);
        CloseWindow(window);
        UnlockPubScreen(NULL, screen);
        CloseLibrary(GadToolsBase);
        CloseLibrary(IntuitionBase);
        return 1;
    }

    AddGList(window, glist, (UWORD)-1, -1, NULL);
    RefreshGadgets(glist, window, NULL);

    BOOL done = FALSE;
    struct IntuiMessage *msg;
    while (!done) {
        WaitPort(window->UserPort);
        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            ULONG class = msg->Class;
            ReplyMsg((struct Message *)msg);
            if (class == IDCMP_CLOSEWINDOW) {
                done = TRUE;
            }
        }
    }

    RemoveGList(window, glist, -1);
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    CloseWindow(window);
    CloseScreen(screen);
    CloseLibrary(GadToolsBase);
    CloseLibrary(IntuitionBase);
    return 0;
}