// Multiplayer transport for the browser build of 0 A.D.
// The engine's network layer (stubs/webenet.cpp) talks to this file; players connect to each other with
// WebRTC data channels, set up through a small signaling broker (PeerJS).  A host shares a short game code,
// friends type that code where the game asks for the host's IP address.
(function () {
  const PREFIX = 'oad27-';
  const ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
  const FRAME = 16000;                       // payload bytes per data-channel message
  const CONNECT_TIMEOUT = 25000;

  // Optional overrides: window.OAD_NET_CONFIG = {signal:{host,port,path,secure,key}, iceServers:[...]}  or  ?signal=host:port
  const cfg = window.OAD_NET_CONFIG || {};
  const iceServers = cfg.iceServers || [
    { urls: 'stun:stun.l.google.com:19302' },
    { urls: 'stun:stun1.l.google.com:19302' },
    { urls: 'stun:global.stun.twilio.com:3478' },
    { urls: ['turn:openrelay.metered.ca:80', 'turn:openrelay.metered.ca:443', 'turn:openrelay.metered.ca:443?transport=tcp'],
      username: 'openrelayproject', credential: 'openrelayproject' },
  ];
  function peerOptions() {
    const o = { debug: 0, config: { iceServers } };
    let s = cfg.signal;
    const q = new URLSearchParams(location.search).get('signal');
    if (q) { const m = /^([^:/]+)(?::(\d+))?(\/.*)?$/.exec(q); if (m) s = { host: m[1], port: +(m[2] || 9000), path: m[3] || '/', secure: m[1] !== 'localhost' && m[1] !== '127.0.0.1' }; }
    if (s) Object.assign(o, s);
    return o;
  }

  const DEBUG = new URLSearchParams(location.search).has('netdebug');
  const dbg = (...a) => { if (DEBUG) console.log('[net]', ...a); };
  // ---- shared-memory ring: records for the engine (JS never calls into the engine's C++ code) ----
  const R_INCOMING = 1, R_CONNECTED = 2, R_CLOSED = 3, R_DATA = 4, R_ALIVE = 5, R_FREE = 6;
  let ring = null;
  const pendingPosts = [];
  let retryTimer = 0;
  const mem = () => new Uint8Array(wasmMemory.buffer);
  function tryPost(r) {
    const len = r.payload ? r.payload.length : 0, size = (32 + len + 7) & ~7;
    const i32 = new Int32Array(wasmMemory.buffer);
    const head = Atomics.load(i32, ring.head) >>> 0, tail = Atomics.load(i32, ring.tail) >>> 0;
    let pos = head & (ring.cap - 1), pad = 0, h = head;
    if (pos + size > ring.cap) pad = ring.cap - pos;
    if (((head - tail) >>> 0) + pad + size > ring.cap) return false;
    if (pad) { const o = (ring.base + pos) >> 2; i32[o] = 0; i32[o + 1] = pad; h = (h + pad) >>> 0; pos = 0; }
    const o = (ring.base + pos) >> 2;
    i32[o] = r.type; i32[o + 1] = size; i32[o + 2] = r.host | 0; i32[o + 3] = r.idx | 0; i32[o + 4] = r.a | 0; i32[o + 5] = r.b | 0; i32[o + 6] = len; i32[o + 7] = 0;
    if (len) new Uint8Array(wasmMemory.buffer).set(r.payload, ring.base + pos + 32);
    Atomics.store(i32, ring.head, (h + size) | 0);
    Atomics.add(i32, ring.wake, 1); Atomics.notify(i32, ring.wake);
    return true;
  }
  function flushPosts() {
    retryTimer = 0;
    if (!ring) return;
    while (pendingPosts.length && tryPost(pendingPosts[0])) pendingPosts.shift();
    if (pendingPosts.length) retryTimer = setTimeout(flushPosts, 2);
  }
  function post(type, host, idx, a, b, payload) {
    pendingPosts.push({ type, host, idx, a, b, payload });
    if (!retryTimer) flushPosts();
  }
  const freeLater = ptr => post(R_FREE, 0, 0, ptr, 0, null);
  const links = new Map();                   // "host:idx" -> link
  let listenPeer = null, listenHost = 0, code = '';
  const incoming = new Map();                // handle -> link waiting for a slot
  let nextHandle = 1;
  const clientPeers = new Map();             // "host:idx" -> Peer (outgoing connection)

  // ---------------------------------------------------------------- UI
  const ui = (function () {
    const el = document.createElement('div');
    el.style.cssText = 'position:fixed;top:8px;left:50%;transform:translateX(-50%);z-index:2147483000;display:none;gap:10px;align-items:center;' +
      'background:#1a0f0fe6;border:1px solid #8a6a2a;border-radius:6px;padding:6px 12px;color:#e2c27a;font:13px system-ui,sans-serif;box-shadow:0 2px 12px #000a';
    el.innerHTML = '<span id="onet-msg"></span><b id="onet-code" style="letter-spacing:.18em;font-size:16px;color:#fff1bd"></b>' +
      '<button id="onet-copy" style="display:none;background:#c9a24a;color:#1a1608;border:0;border-radius:4px;padding:3px 10px;font:600 12px system-ui;cursor:pointer">Copy</button>';
    document.addEventListener('DOMContentLoaded', () => document.body.appendChild(el));
    if (document.body) document.body.appendChild(el);
    const $ = id => el.querySelector(id);
    let timer = 0;
    return {
      hosting(c, n) {
        clearTimeout(timer);
        el.style.display = 'flex';
        $('#onet-msg').textContent = 'Game code:';
        $('#onet-code').textContent = c.slice(0, 4) + '-' + c.slice(4);
        const b = $('#onet-copy'); b.style.display = '';
        b.onclick = () => { navigator.clipboard.writeText(c).then(() => { b.textContent = 'Copied'; setTimeout(() => (b.textContent = 'Copy'), 1500); }).catch(() => prompt('Game code:', c)); };
        if (n) $('#onet-msg').textContent = 'Game code (' + n + ' connected):';
      },
      note(text, ms) {
        clearTimeout(timer);
        el.style.display = 'flex'; $('#onet-code').textContent = ''; $('#onet-copy').style.display = 'none'; $('#onet-msg').textContent = text;
        if (ms) timer = setTimeout(() => { el.style.display = 'none'; }, ms);
      },
      hide() { clearTimeout(timer); el.style.display = 'none'; },
    };
  })();

  // ---------------------------------------------------------------- framing
  function u32(v, o) { return v[o] | (v[o + 1] << 8) | (v[o + 2] << 16) | ((v[o + 3] << 24) >>> 0); }
  function put32(v, o, x) { v[o] = x & 255; v[o + 1] = (x >>> 8) & 255; v[o + 2] = (x >>> 16) & 255; v[o + 3] = (x >>> 24) & 255; }

  function pump(l) {
    const dc = l.conn.dataChannel;
    if (!dc || dc.readyState !== 'open') return;
    while (l.queue.length && dc.bufferedAmount < 512 * 1024) {
      try { dc.send(l.queue.shift()); } catch (e) { break; }
    }
  }
  function sendRaw(l, frame) { l.queue.push(frame); pump(l); }
  function sendControl(l, type, value) {
    const f = new Uint8Array(5); f[0] = type; put32(f, 1, value >>> 0); sendRaw(l, f);
  }

  function attach(l) {
    const dc = l.conn.dataChannel;
    dc.binaryType = 'arraybuffer';
    dc.bufferedAmountLowThreshold = 128 * 1024;
    dc.addEventListener('bufferedamountlow', () => pump(l));
    l.conn.on('data', d => { const f = new Uint8Array(d.buffer ? (d.buffer.slice(d.byteOffset, d.byteOffset + d.byteLength)) : d); if (l.early) l.early.push(f); else onFrame(l, f); });
    const gone = (e) => {
      dbg('link gone', l.key, e && e.type || e);
      if (l.closed) return; l.closed = true; clearInterval(l.ping); links.delete(l.key);
      if (l.idx >= 0) post(R_CLOSED, l.host, l.idx, l.reason >>> 0, 0, null);
      updateCount();
    };
    l.conn.on('close', gone); l.conn.on('error', gone);
    l.ping = setInterval(() => { const f = new Uint8Array(5); f[0] = 2; put32(f, 1, performance.now() | 0); sendRaw(l, f); }, 500);
  }

  function onFrame(l, f) {
    if (l.closed || !f.length) return;
    switch (f[0]) {
      case 0: {                              // data fragment: [0][total u32][offset u32][payload]
        const total = u32(f, 1), off = u32(f, 5), n = f.length - 9;
        if (off === 0) { l.rx = true; l.got = 0; }
        if (!l.rx) return;
        l.got += n;
        post(R_DATA, l.host, l.idx, total, off, f.subarray(9));
        if (l.got >= total) l.rx = null;
        break;
      }
      case 1: dbg('disconnect notice from peer', l.key, u32(f, 1)); l.reason = u32(f, 1); setTimeout(() => { try { l.conn.close(); } catch (e) {} }, 0); break;   // disconnect notice
      case 2: { const r = new Uint8Array(5); r[0] = 3; r.set(f.subarray(1, 5), 1); sendRaw(l, r); post(R_ALIVE, l.host, l.idx, 0, 0, null); break; }  // ping
      case 3: { const rtt = Math.max(0, ((performance.now() | 0) - u32(f, 1)) | 0); post(R_ALIVE, l.host, l.idx, rtt + 1, 0, null); break; }           // pong
    }
  }

  function newLink(host, idx, conn) {
    const l = { host, idx, key: host + ':' + idx, conn, queue: [], rx: null, got: 0, reason: 0, closed: false, ping: 0 };
    links.set(l.key, l); attach(l); return l;
  }
  function updateCount() {
    if (!listenPeer) return;
    let n = 0; links.forEach(l => { if (l.host === listenHost) n++; });
    ui.hosting(code, n);
  }

  // ---------------------------------------------------------------- API used by the engine
  const api = {
    get code() { return code; },
    get status() { return { hosting: !!listenPeer, code, links: links.size }; },

    listen(host) {
      dbg('listen', host);
      this.unlisten(listenHost);
      listenHost = host;
      let tries = 0;
      const start = () => {
        code = Array.from(crypto.getRandomValues(new Uint8Array(8)), b => ALPHABET[b % 32]).join('');
        let p;
        try { p = new Peer(PREFIX + code, peerOptions()); } catch (e) { ui.note('Online play unavailable: ' + e.message, 8000); return; }
        listenPeer = p;
        p.on('open', () => { updateCount(); window.dispatchEvent(new CustomEvent('oad-net-hosting', { detail: code })); });
        p.on('connection', conn => {
          conn.on('open', () => {
            // The engine assigns a peer slot asynchronously (see assigned()); frames that arrive meanwhile are held back.
            const handle = nextHandle++;
            const l = { host: listenHost, idx: -1, key: '', conn, queue: [], rx: null, got: 0, reason: 0, closed: false, ping: 0, early: [] };
            incoming.set(handle, l); attach(l);
            dbg('incoming connection, handle', handle, 'from', conn.peer, conn.connectionId);
            post(R_INCOMING, listenHost, handle, 0, 0, null);
          });
        });
        p.on('error', err => {
          if (err.type === 'unavailable-id' && tries++ < 5) { try { p.destroy(); } catch (e) {} listenPeer = null; start(); return; }
          if (p === listenPeer && !p.open) ui.note('Could not reach the matchmaking server - only local players can join (' + err.type + ')', 12000);
        });
      };
      start();
    },
    unlisten(host) {
      if (listenPeer && (host === listenHost || !host)) { try { listenPeer.destroy(); } catch (e) {} listenPeer = null; ui.hide(); }
    },

    init(base, cap, headPtr, tailPtr) {
      ring = { base, cap, head: headPtr >> 2, tail: tailPtr >> 2, wake: (tailPtr >> 2) + 1 };
      flushPosts();
    },

    assigned(handle, idx) {
      const l = incoming.get(handle); incoming.delete(handle);
      if (!l) return;
      if (idx < 0) { l.closed = true; clearInterval(l.ping); try { l.conn.close(); } catch (e) {} return; }
      l.idx = idx; l.key = l.host + ':' + idx;
      if (l.closed) { post(R_CLOSED, l.host, idx, 0, 0, null); return; }
      links.set(l.key, l); updateCount();
      const early = l.early; l.early = null;
      for (const f of early) onFrame(l, f);
    },

    connect(host, idx, text, strPtr) {
      freeLater(strPtr);
      dbg('connect', host, idx, text);
      const c = String(text).toUpperCase().replace(/[^A-Z0-9]/g, '');
      const key = host + ':' + idx;
      const fail = msg => {
        const p = clientPeers.get(key); clientPeers.delete(key); if (p) { try { p.destroy(); } catch (e) {} }
        if (links.has(key)) return;
        ui.note(msg, 8000); post(R_CLOSED, host, idx, 0, 0, null);
      };
      if (c.length !== 8) { fail('That is not a valid game code (8 letters/digits).'); return; }
      ui.note('Connecting to ' + c.slice(0, 4) + '-' + c.slice(4) + ' ...');
      let p;
      const myId = 'oadc-' + Array.from(crypto.getRandomValues(new Uint8Array(10)), b => ALPHABET[b % 32]).join('');   // own id: avoids the broker's HTTP id call
      try { p = new Peer(myId, peerOptions()); } catch (e) { fail('Online play unavailable: ' + e.message); return; }
      clientPeers.set(key, p);
      const timer = setTimeout(() => fail('Could not connect (timed out). The host may be offline or behind a strict firewall.'), CONNECT_TIMEOUT);
      p.on('error', err => { clearTimeout(timer); fail(err.type === 'peer-unavailable' ? 'No game found with that code.' : 'Connection error: ' + err.type); });
      p.on('open', () => {
        const conn = p.connect(PREFIX + c, { reliable: true, serialization: 'raw' });
        conn.on('open', () => { dbg('outgoing open'); clearTimeout(timer); newLink(host, idx, conn); ui.note('Connected.', 1500); post(R_CONNECTED, host, idx, 0, 0, null); });
        conn.on('error', () => { clearTimeout(timer); fail('Connection failed.'); });
      });
    },

    send(host, idx, ptr, len) {
      const l = links.get(host + ':' + idx);
      if (!l || l.closed) { freeLater(ptr); return; }
      const data = mem().slice(ptr, ptr + len); freeLater(ptr);
      if (len === 0) { const f = new Uint8Array(9); put32(f, 1, 0); put32(f, 5, 0); sendRaw(l, f); return; }
      for (let off = 0; off < len; off += FRAME) {
        const n = Math.min(FRAME, len - off), f = new Uint8Array(9 + n);
        put32(f, 1, len); put32(f, 5, off); f.set(data.subarray(off, off + n), 9);
        l.queue.push(f);
      }
      pump(l);
    },

    disconnect(host, idx, reason) {
      dbg('engine disconnect', host, idx, reason);
      const key = host + ':' + idx, l = links.get(key);
      const p = clientPeers.get(key);
      if (!l) { if (p) { clientPeers.delete(key); try { p.destroy(); } catch (e) {} } return; }
      l.closed = true; clearInterval(l.ping); links.delete(key);
      sendControl(l, 1, reason);
      setTimeout(() => { try { l.conn.close(); } catch (e) {} if (p) { clientPeers.delete(key); try { p.destroy(); } catch (e) {} } updateCount(); }, 250);
    },
  };
  window.OADNet = api;
})();
