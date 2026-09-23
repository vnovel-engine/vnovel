// engine_cabi.h — C ABI for engine backends.
// Each engine (ONScripter-EN, PONScripter) compiles as a static library
// with -fvisibility=hidden and exports only these C functions.
// This avoids C++ namespace/mangling issues entirely.

#ifndef ENGINE_CABI_H
#define ENGINE_CABI_H

#ifdef __cplusplus
extern "C" {
#endif

/* All functions take a handle returned by engine_create. */
typedef void* EngineHandle;

/* Factory functions — implemented in engine_ons_cabi.cpp / engine_pons_cabi.cpp */
EngineHandle ons_engine_create(void);
EngineHandle pons_engine_create(void);

/* Common interface — both engines implement all of these */
typedef struct {
    EngineHandle (*create)(void);
    void         (*destroy)(EngineHandle);
    int          (*init)(EngineHandle);
    void         (*executeLabel)(EngineHandle);
    void         (*resetCommand)(EngineHandle);
    void         (*setArchivePath)(EngineHandle, const char*);
    void         (*setSavePath)(EngineHandle, const char*);
    void         (*setKeyEXE)(EngineHandle, const char*);
    void         (*setDebugLevel)(EngineHandle, int);
    int          (*getWidth)(EngineHandle);
    int          (*getHeight)(EngineHandle);
    int          (*isAutoMode)(EngineHandle);
    int          (*isSkipMode)(EngineHandle);
} EngineVTable;

extern const EngineVTable ons_engine_vtable;
extern const EngineVTable pons_engine_vtable;

#ifdef __cplusplus
}
#endif

#endif
