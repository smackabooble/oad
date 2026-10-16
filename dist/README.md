# 0 A.D. in your browser (unofficial WebAssembly port)

This runs the real 0 A.D. engine (compiled to WebAssembly) in a browser tab, using **your own copy of the game's
data**. Nothing is uploaded anywhere; the local server only reads your game folder.

## Requirements
- A Chromium-based browser **version 137 or newer** (Chrome, Edge, Brave, ...). The engine uses WebAssembly
  threads and JSPI. Firefox/Safari are not supported yet.
- A GPU/driver with **WebGL 2**.
- Python 3.8+ (only for the tiny local server).
- 0 A.D. game data from an official release matching this build: **Alpha 27 "Agni"** (the `mods/public/public.zip`
  and `mods/mod/mod.zip` files must be present; they are normal parts of every release/installer).

## Run
```
python3 serve.py --data "<path to your 0 A.D. data folder>"
```
The data folder is the one that contains `mods/` and `config/`:

| OS | typical location |
|----|------------------|
| Windows | `C:\Program Files\0 A.D.\binaries\data` |
| Linux | `/usr/share/0ad/data` or `<install>/binaries/data` |
| macOS | `/Applications/0 A.D.app/Contents/Resources/data` |

Then open **http://localhost:8080/** (use `--port` to change it). Use the same address each time: settings and
saved games are kept in your browser (origin-private storage) per address.

## Tips
- Press **F11** for fullscreen; **Alt+F** shows the FPS counter.
- Add `?debug` to the URL to see the engine log on screen.
- Jump straight into a match for testing: `http://localhost:8080/?args=-autostart=scenarios/arcadia%20-autostart-players=2`
- First start downloads only what the game touches from the local server, so it is fast; there is no big upload.

## Known limitations
- Multiplayer lobby, mod downloading, the scenario editor (Atlas) and user reports are disabled.
- Chromium only (see requirements). Mobile browsers are not supported.
- Performance depends on your GPU; software rendering is very slow.
