# 0 A.D. in the browser (WASM + QuickJS) — work in progress

Goal: build the real 0 A.D. engine for the browser with Emscripten, replacing SpiderMonkey 91
(no WASM build exists) with QuickJS via a compatibility shim.

## Layout
- `shim/` — SpiderMonkey-91-compatible API subset on QuickJS (quickjs-ng v0.10.1).
  Values, Rooted/Handle types, natives (trampoline), properties, arrays, strings, JSON, classes.
  Rooting model: Rooted/Handle are plain storage; values created during a request live in a
  per-thread pool released by the outermost ScriptRequest. Heap/PersistentRooted hold real refs.
- `patches/` — patches against upstream 0ad/0ad (`git am`).

## Status (phase 3 of 8)
Compiles against the shim: JSON.cpp, ScriptExceptions.cpp, ScriptStats.cpp.
Todo: ScriptConversions.cpp (string chars), ScriptInterface.cpp (realm/global/eval),
Promises.cpp + ScriptContext.cpp (job queue), StructuredClone.cpp, GUI proxy (exotic objects).

## Notes
- Needs `-sSTACK_SIZE=4MB` and `JS_SetMaxStackSize(rt, 3MB)` or QuickJS reports stack overflow.
- Boost headers: flatten `libs/*/include/boost` into one include dir.
- Egress proxy blocks github.com `archive/` URLs (Emscripten ports); use git clone or release assets.
