// util_stub.c — Stub for Fate's util.dll plugin
// Provides empty V2Link/V2Unlink so Plugins.link("util.dll") doesn't crash.
//
// Build: gcc -shared -fPIC -o util.dll util_stub.c
// Place in the game directory.

__attribute__((visibility("default")))
int V2Link(void *exporter)
{
    /* Stub — no functions registered */
    return 0;
}

__attribute__((visibility("default")))
int V2Unlink(void)
{
    return 0;
}
