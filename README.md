# 0 A.D. in the browser (WASM + QuickJS) — work in progress

Goal: build the real 0 A.D. engine for the browser with Emscripten, replacing SpiderMonkey 91
(no WASM build exists) with QuickJS via a compatibility shim.

## Layout
- `shim/` — SpiderMonkey-91-compatible API subset on QuickJS (quickjs-ng v0.10.1).
  Values, Rooted/Handle types, natives (trampoline), properties, arrays, strings, JSON, classes.
  Rooting model: Rooted/Handle are plain storage; values created during a request live in a
  per-thread pool released by the outermost ScriptRequest. Heap/PersistentRooted hold real refs.
- `patches/` — patches against upstream 0ad/0ad (`git am`).

## Status
- Phase 2 (libraries) done: zlib, libpng, freetype, ogg/vorbis, SDL2, ICU, libsodium, ENet, fmt, libxml2, Boost.Filesystem.
- Phase 3 (SpiderMonkey -> QuickJS) done: all JS-API users compile against the shim.
- Phase 4 (whole engine) done: all 445 engine sources compile with `-pthread -fwasm-exceptions` and link to pyrogenesis.wasm.
  **Milestone:** under Node, headless (`-autostart=scenarios/arcadia -autostart-nonvisual`), the real game loads the map,
  all entity templates and all JS simulation components on QuickJS, prints `GAME STARTED, ALL INIT COMPLETE` with no
  errors, and runs the simulation (~240 turns/s). Threads, C++ exceptions, VFS, config, QuickJS shim all work.
- Stubbed for the browser: lobby (gloox), Atlas editor, Vulkan, X11, curl (mod.io / user reports), dir watching, NVTT, UPnP.
- Next (phase 5-8): WebGL2 rendering + browser main loop, game data delivery (lazy fetch), audio, packaging.

## Build
1. `build/build_deps.sh` builds dependencies (needs emsdk, sources in deps/).
2. `python3 build/gen_ninja.py && ninja -C build_mt && build_mt/link.sh pyrogenesis.html`.

## Notes
- Needs `-sSTACK_SIZE=4MB` and `JS_SetMaxStackSize(rt, 3MB)` or QuickJS reports stack overflow.
- Boost headers: flatten `libs/*/include/boost` into one include dir.
- Egress proxy blocks github.com `archive/` URLs (Emscripten ports); use git clone or release assets.
