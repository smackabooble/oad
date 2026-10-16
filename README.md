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
- Phase 2 (libraries) done: zlib, libpng, freetype, ogg/vorbis, SDL2, ICU (+ENet, fmt, libxml2) built for WASM.
- Phase 3 (SpiderMonkey -> QuickJS) done at compile level: 73 of the 84 engine files that use the JS API
  compile against the shim for wasm32; the other 11 only lack non-JS libraries/headers
  (curl, sodium, miniupnpc, nvtt, aio.h, glad/vulkan) or a PCH include.
- Nothing is linked or run yet. Next: phase 4 (build the whole engine, stub online features), then WebGL2.

## Notes
- Needs `-sSTACK_SIZE=4MB` and `JS_SetMaxStackSize(rt, 3MB)` or QuickJS reports stack overflow.
- Boost headers: flatten `libs/*/include/boost` into one include dir.
- Egress proxy blocks github.com `archive/` URLs (Emscripten ports); use git clone or release assets.
