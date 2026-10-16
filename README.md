# 0 A.D. in the browser (unofficial WebAssembly port)

Runs the real 0 A.D. engine (Alpha 27 "Agni") in a Chromium-based browser tab, using your own copy of the game data.

**To play:** `python3 dist/serve.py --data "<your 0 A.D. data folder>"` and open http://localhost:8080/ — see `dist/README.md`.

# 0 A.D. in the browser (WASM + QuickJS) — work in progress

Goal: build the real 0 A.D. engine for the browser with Emscripten, replacing SpiderMonkey 91
(no WASM build exists) with QuickJS via a compatibility shim.

## Layout
- `shim/` — SpiderMonkey-91-compatible API subset on QuickJS (quickjs-ng v0.10.1).
  Values, Rooted/Handle types, natives (trampoline), properties, arrays, strings, JSON, classes.
  Rooting model: Rooted/Handle are plain storage; values created during a request live in a
  per-thread pool released by the outermost ScriptRequest. Heap/PersistentRooted hold real refs.
- `patches/` — the browser-port patch for the 0 A.D. Release 27.1 source (applied by `build/setup_sources.sh`).

## Status
- Phase 2 (libraries) done: zlib, libpng, freetype, ogg/vorbis, SDL2, ICU, libsodium, ENet, fmt, libxml2, Boost.Filesystem.
- Phase 3 (SpiderMonkey -> QuickJS) done: all JS-API users compile against the shim.
- Phase 4 (whole engine) done: all 445 engine sources compile with `-pthread -fwasm-exceptions` and link to pyrogenesis.wasm.
  **Milestone:** under Node, headless (`-autostart=scenarios/arcadia -autostart-nonvisual`), the real game loads the map,
  all entity templates and all JS simulation components on QuickJS, prints `GAME STARTED, ALL INIT COMPLETE` with no
  errors, and runs the simulation (~240 turns/s). Threads, C++ exceptions, VFS, config, QuickJS shim all work.
- **Milestone 2: the real 0 A.D. main menu renders in headless Chromium** (see `web/screenshot-main-menu.png`):
  game thread in a worker (PROXY_TO_PTHREAD + JSPI frame yield), WebGL2 via a patched SDL (`sdl/`),
  game data fetched lazily from zip archives with HTTP Range requests (WasmFS fetch backend).
- **Milestone 3: an actual match renders and runs in the browser** (`web/screenshot-in-game.png`): terrain, shadows,
  buildings, units, minimap, HUD. glad regenerated for GLES 3.0, WebGL2 texture/depth formats, framebuffer blit,
  FCollada linked statically (no dlopen), lobby stubs registered, GLSL ES loop fix.
- Stubbed for the browser: lobby (gloox), Atlas editor, Vulkan, X11, curl (mod.io / user reports), dir watching, NVTT
  (replaced by an uncompressed-DDS fallback converter), UPnP, audio (not yet).
- **Milestone 4 (current): playable package.** Release build in `dist/` (9 MB wasm), `serve.py` launcher that serves the
  user's own game data with Range support + COOP/COEP, audio (OpenAL/Web Audio, unlocked on first click), persistent
  settings/saves/texture cache (OPFS), resize handling, mouse/keyboard input verified in headless Chromium.
  Verified: main menu + submenus, starting a match via autostart, selecting units, camera, FPS overlay.
- Not verifiable in this environment: real-GPU performance (tests ran on a software GL emulator: ~15 FPS at 480x270),
  audible sound, DXT-compressed textures from an official release's pre-converted archives (only raw source textures,
  converted by the built-in fallback, were available), Firefox/Safari (need JSPI, Chromium 137+ only).

## Build
1. `build/build_deps.sh` builds dependencies (needs emsdk, sources in deps/). SDL needs the patch in `sdl/`.
2. `python3 build/gen_ninja.py && ninja -C build_mt`, then link with `web/iter.sh` (it also runs a Chromium smoke test).
3. Game data: `web/mkzip.py` packs a loose mod into <1.9 GB zips (the engine has no Zip64); `web/gen_manifest.py`
   lists the files; `web/server.py` serves with Range + COOP/COEP headers (needed for threads).

## Notes
- Needs `-sSTACK_SIZE=4MB` and `JS_SetMaxStackSize(rt, 3MB)` or QuickJS reports stack overflow.
- Boost headers: flatten `libs/*/include/boost` into one include dir.
- Egress proxy blocks github.com `archive/` URLs (Emscripten ports); use git clone or release assets.
