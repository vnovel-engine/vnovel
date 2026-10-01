# Sound quality

Ensure the decoding sample rate and the playback sample rate are aligned
as much as possible. If live resampling is needed, we should do it. If
our engine can support dynamic sample rate, we should.

The default target is CD, i.e. 44100 Hz 16-bit PCM audio.

# KiriKiri support

https://steamdb.info/tech/Engine/KiriKiri/ -> this opens these games
See in ~/dev: krkrz-engine, krkrsdl2 for porting efforts.

Would open support for the original Fata Morgana, Fate/Stay Night...

## Architecture

krkrsdl2 (MIT-licensed, SDL2-based KiriKiri port) builds and runs
on Linux. It uses OpenGL via SDL2 (SDL_CreateWindow +
SDL_GL_CreateContext). Entry point: krkrsdl2_run_main_loop().

### Approach: HW render context + SDL2 dummy backend

1. Use RETRO_ENVIRONMENT_SET_HW_RENDER to get a real GL context
   from RetroArch (like mupen64plus, parallel-n64).
2. Create a custom SDL2 video driver (sdl2-libretro/) that:
   + Returns the RetroArch GL context from SDL_GL_CreateContext
   + Bridges SDL_GL_SwapWindow to video_cb
   + Bridges input from input_poll_cb to SDL2 events
   + Bridges audio from audio_batch_cb to SDL2 audio callback
3. Create engine_kirikiri_cabi.cpp — wraps krkrsdl2_run_main_loop()
   in the EngineVTable interface.
4. Add Makefile target for krkrsdl2 engine.

### XP3 decryption

krkrsdl2 can read XP3 archives natively. The cxdec.tpm plugin
(Windows DLL) provides custom decompression — may need a native
replacement or stub. Some games use encrypted XP3s that need
the decryption key from the .exe (GARbro / arc_unpacker can
extract with --plugin=fsn).

### Game tested

Fate/Stay Night (English patch) — extracted to
~/VN/Fate/extracted/. Runs with SDL_VIDEODRIVER=dummy
(headless, doesn't crash).
# Better engine detection than just filename extension

Parse (safely) the executable/DLL to identify engine, version, and initialize the
proper VTable for each.

# Save state/load state

(unsure if possible but theoretical plan below)

When requesting a save-state:
- post a freeze-engine request to the engine
- start the save-state worker (which listens to the engine-frozen semaphore)

When requesting a load-state:
- post a freeze-engine request to the engine
- start the load-state worker (which listens to the engine-frozen semaphore)

In each engine top-loop: if freeze-engine request is not posted, go as usual.
Else:
  - post to the engine-frozen semaphore
  - listen to the state-hanled semaphore
  - unset the engine-frozen flag
  - restart top-loop

Since the engines should (to verify) count only on their state
(and the accessory audio playhead state) to operate, we should be good.

In the save-state worker:
  - block on the engine-frozen semaphore, and upon acquisition
    - dump engine state memory
    - dump audio playhead
    - persist to file
    - post to the state-handled semaphore

The load-state worker has similar sync mechanisms as the save-state worker:
  - block on the engine-frozen semaphore, and upon acquisition
    - load engine state and audio playhead state from file
    - set engine memory from dump
    - set audio playhead from dump
    - post to the state-handled semaphore

Good things: this does not alter external resources such as save files (this would
be bad). this increases testability and allows for mid-sentence saves, allows rewind
and so on.

To be defined for each engine: what *is* its (logical) state. i.e. if i want to get
an exact frame from any point in the game's render lifetime, what do i need to set?

See [ENGINE-STATE.md](ENGINE-STATE.md) for a per-engine breakdown of logical state.
