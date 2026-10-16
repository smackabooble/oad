// Service worker for the browser build of 0 A.D.
//  1. Adds the cross-origin-isolation headers WebAssembly threads need (GitHub Pages cannot set headers).
//  2. Serves the downloaded game data (stored in the browser's origin-private file system) to the engine
//     under  data/...  with HTTP Range support, so the engine reads it exactly like a normal web server.
const DATA_DIR = 'gamedata';
const scopePath = new URL(self.registration.scope).pathname;

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', e => e.waitUntil(self.clients.claim()));

async function opfsFile(rel) {
  try {
    let dir = await navigator.storage.getDirectory();
    const parts = (DATA_DIR + '/' + rel).split('/').filter(Boolean);
    const name = parts.pop();
    for (const p of parts) dir = await dir.getDirectoryHandle(p);
    return await (await dir.getFileHandle(name)).getFile();
  } catch (e) { return null; }
}

function isolate(resp) {
  if (!resp || resp.status === 0 || resp.type === 'opaque') return resp;
  const h = new Headers(resp.headers);
  h.set('Cross-Origin-Embedder-Policy', 'require-corp');
  h.set('Cross-Origin-Opener-Policy', 'same-origin');
  h.set('Cross-Origin-Resource-Policy', 'cross-origin');
  return new Response(resp.body, { status: resp.status, statusText: resp.statusText, headers: h });
}

const CORP = { 'Cross-Origin-Resource-Policy': 'cross-origin', 'Cross-Origin-Embedder-Policy': 'require-corp',
               'Cross-Origin-Opener-Policy': 'same-origin', 'Accept-Ranges': 'bytes', 'Cache-Control': 'no-store' };

async function serveData(req, rel) {
  const file = await opfsFile(rel);
  if (!file) return null;
  const size = file.size;
  const type = rel.endsWith('.zip') ? 'application/zip' : 'application/octet-stream';
  const range = req.headers.get('Range');
  const m = range && /^bytes=(\d*)-(\d*)/.exec(range);
  if (!m) {
    if (req.method === 'HEAD') return new Response(null, { status: 200, headers: { ...CORP, 'Content-Type': type, 'Content-Length': String(size) } });
    return new Response(file, { status: 200, headers: { ...CORP, 'Content-Type': type, 'Content-Length': String(size) } });
  }
  let start = m[1] ? parseInt(m[1], 10) : 0;
  let end = m[2] ? parseInt(m[2], 10) : size - 1;
  if (!m[1] && m[2]) { start = Math.max(0, size - parseInt(m[2], 10)); end = size - 1; }
  end = Math.min(end, size - 1);
  if (start > end) return new Response(null, { status: 416, headers: { ...CORP, 'Content-Range': `bytes */${size}` } });
  const headers = { ...CORP, 'Content-Type': type, 'Content-Range': `bytes ${start}-${end}/${size}` };
  if (req.method === 'HEAD') { headers['Content-Length'] = String(size); return new Response(null, { status: 206, headers }); }
  return new Response(file.slice(start, end + 1), { status: 206, headers: { ...headers, 'Content-Length': String(end - start + 1) } });
}

self.addEventListener('fetch', event => {
  const req = event.request;
  if (req.cache === 'only-if-cached' && req.mode !== 'same-origin') return;
  const url = new URL(req.url);
  if (url.origin !== location.origin) return;           // leave cross-origin requests (the data host) alone
  const path = url.pathname.replace(/\/{2,}/g, '/');
  if (!path.startsWith(scopePath)) return;
  const rel = path.slice(scopePath.length);
  event.respondWith((async () => {
    try {
      if (rel.startsWith('data/')) {
        const r = await serveData(req, rel.slice(5));
        if (r) return r;
      } else if (rel === 'data_manifest.txt') {
        const f = await opfsFile('_manifest.txt');
        if (f) return new Response(f, { status: 200, headers: { ...CORP, 'Content-Type': 'text/plain' } });
      }
      return isolate(await fetch(req));
    } catch (e) {
      return new Response('Service worker error: ' + e, { status: 502 });
    }
  })());
});
