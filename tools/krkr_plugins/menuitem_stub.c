// menuitem_stub.c — Native KiriKiri SDL2 plugin that registers MenuItem class.
// This provides the MenuItem native class that KAG3's KAGMenuItem extends.
// On Windows, MenuItem is a native window menu class. On Linux, we stub it.
//
// Build: gcc -shared -fPIC -o menuitem.tpm menuitem_stub.c
// Auto-loaded before data.xp3 is opened.

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* Minimal iTVPFunctionExporter interface - we only need TVPGetScriptDispatch */
typedef struct iTVPFunctionExporter iTVPFunctionExporter;

/* The function exporter provides function pointers by index.
 * We need TVPGetScriptDispatch to register classes. */

/* V2Link receives the exporter. We call through it to get the script dispatch,
 * then register the MenuItem class. But this is complex C ABI work.
 * 
 * Simpler approach: just call TVPGetScriptDispatch directly since we're
 * linked into the same process with ENABLE_EXPORTS. */

/* Forward declarations - these are in krkrsdl2's binary */
extern void *TVPGetScriptDispatch(void);
extern int TJS_E_BADPARAMCOUNT;
extern int TJS_S_OK;

/* TJS2 class registration via iTJSDispatch2 is complex.
 * Instead, we'll execute TJS code that defines the class.
 * We can use Scripts.execStorage or Scripts.evalExpression. */

/* Actually, the simplest approach: write a startup.tjs that defines MenuItem
 * before loading any other scripts. The plugin just needs to be loaded. */

__attribute__((visibility("default")))
int V2Link(void *exporter)
{
    /* The plugin being loaded is enough — we don't need to register anything.
     * The MenuItem class will be defined in a TJS script that loads before
     * Menus.tjs. See: system/MenuItem.tjs */
    fprintf(stderr, "menuitem_stub: loaded\n");
    return 0;
}

__attribute__((visibility("default")))
int V2Unlink(void)
{
    return 0;
}
