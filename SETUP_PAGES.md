# Publish steps (do once)

1. **Game data host** (needs CORS + Range). Easiest: Hugging Face dataset.
   - Make a free account, create a *dataset* repo (e.g. `you/0ad-data`).
   - `pip install huggingface_hub && huggingface-cli login`
   - From your official 0 A.D. `data` folder: `huggingface-cli upload you/0ad-data ./data . --repo-type dataset`
   - Base URL: `https://huggingface.co/datasets/you/0ad-data/resolve/main/`
   - Keep each mod zip under 1.9 GB (`web/mkzip.py` splits them).
2. **Config:** `python3 tools/make_data_config.py ./data <BASE URL> docs/data-config.json`, commit, push.
3. **Repo public**, then Settings → Pages → Branch `main`, folder `/docs`.
4. Open `https://<user>.github.io/oad/` in Chrome/Edge 137+, click "Download and play".

Reset stored data: add `?reset` to the URL.

## Multiplayer

Works in the browser between two or more players:

1. **Host:** Multiplayer → *Host New Game* → Continue. A box at the top shows your **game code** (e.g. `K7QX-4M2P`, with a Copy button). Send it to your friends.
2. **Join:** Multiplayer → *Join Game*. Type the host's game code into the **"Server Hostname or IP"** box (leave the port), pick any name, Continue.
3. Everyone clicks *I'm ready*; the host clicks *Start Game*.

How it works: the engine's network layer is replaced by `stubs/webenet.cpp` (same API as ENet), players connect with WebRTC data channels
(`site/net.js`, signaling through the free PeerJS broker, STUN/TURN for NAT traversal). Nothing needs to be installed or port-forwarded.
If the broker is blocked or you want your own, run a PeerServer and open the site with `?signal=host:port` or set
`window.OAD_NET_CONFIG = {signal: {host, port, path, secure}}` before `net.js` loads.
The lobby (public server list) is not available.
