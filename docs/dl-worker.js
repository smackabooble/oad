// Downloads the game data into the browser's origin-private file system (resumable, parallel range requests).
// Messages in : { cmd: 'start', files: [{path, size, url}], version }  |  { cmd: 'wipe' }
// Messages out: { type: 'progress', done, total, file, bytesPerSec } | { type: 'done' } | { type: 'error', message }
const DATA_DIR = 'gamedata';
const CHUNK = 16 * 1024 * 1024;
const PARALLEL = 4;

async function dirFor(path, create) {
  let dir = await navigator.storage.getDirectory();
  const parts = (DATA_DIR + '/' + path).split('/').filter(Boolean);
  const name = parts.pop();
  for (const p of parts) dir = await dir.getDirectoryHandle(p, { create });
  return { dir, name };
}

async function writeSmall(path, text) {
  const { dir, name } = await dirFor(path, true);
  const h = await (await dir.getFileHandle(name, { create: true })).createSyncAccessHandle();
  const bytes = new TextEncoder().encode(text);
  h.truncate(0); h.write(bytes, { at: 0 }); h.flush(); h.close();
}

async function fileSize(path) {
  try { const { dir, name } = await dirFor(path, false); return (await (await dir.getFileHandle(name)).getFile()).size; }
  catch (e) { return -1; }
}

async function fetchRange(url, start, end, tries = 6) {
  let lastErr;
  for (let i = 0; i < tries; i++) {
    try {
      const r = await fetch(url, { headers: { Range: `bytes=${start}-${end}` }, mode: 'cors', cache: 'no-store' });
      if (r.status !== 206 && r.status !== 200) throw new Error(`HTTP ${r.status} for ${url}`);
      const buf = await r.arrayBuffer();
      if (r.status === 200) {                       // server ignored Range: slice it
        if (buf.byteLength < end + 1 - start && buf.byteLength <= end) throw new Error('short response');
        return buf.slice(start, end + 1);
      }
      if (buf.byteLength !== end - start + 1) throw new Error(`short range response (${buf.byteLength})`);
      return buf;
    } catch (e) { lastErr = e; await new Promise(res => setTimeout(res, 800 * (i + 1))); }
  }
  throw lastErr;
}

let doneBytes = 0, totalBytes = 0, t0 = 0, lastReport = 0;
function report(file, force) {
  const now = performance.now();
  if (!force && now - lastReport < 250) return;
  lastReport = now;
  const sec = Math.max(0.001, (now - t0) / 1000);
  postMessage({ type: 'progress', done: doneBytes, total: totalBytes, file, bytesPerSec: (doneBytes - startBytes) / sec });
}
let startBytes = 0;

async function downloadFile(entry) {
  const { dir, name } = await dirFor(entry.path, true);
  const fh = await dir.getFileHandle(name, { create: true });
  const h = await fh.createSyncAccessHandle();
  try {
    let have = h.getSize();
    if (have >= entry.size) { if (have > entry.size) h.truncate(entry.size); doneBytes += entry.size; report(entry.path, true); return; }
    let off = have - (have % CHUNK);          // resume at a chunk boundary
    doneBytes += off;
    while (off < entry.size) {
      const jobs = [];
      for (let i = 0; i < PARALLEL && off + i * CHUNK < entry.size; i++) {
        const s = off + i * CHUNK, e = Math.min(s + CHUNK, entry.size) - 1;
        jobs.push(fetchRange(entry.url, s, e).then(buf => ({ s, buf })));
      }
      const results = await Promise.all(jobs);
      for (const { s, buf } of results) { h.write(new Uint8Array(buf), { at: s }); doneBytes += buf.byteLength; }
      h.flush();
      off += jobs.length * CHUNK;
      report(entry.path, false);
    }
    h.truncate(entry.size); h.flush();
  } finally { h.close(); }
}

async function start(files, version) {
  totalBytes = files.reduce((a, f) => a + f.size, 0);
  doneBytes = 0; startBytes = 0; t0 = performance.now();
  // already-complete bytes (so speed/ETA reflect only what is downloaded now)
  for (const f of files) { if ((await fileSize(f.path)) === f.size) startBytes += f.size; }
  for (const f of files) await downloadFile(f);
  await writeSmall('_manifest.txt', files.map(f => f.path).join('\n') + '\n');
  await writeSmall('_complete.json', JSON.stringify({ version, time: Date.now(), bytes: totalBytes }));
  postMessage({ type: 'done' });
}

async function wipe() {
  const root = await navigator.storage.getDirectory();
  try { await root.removeEntry(DATA_DIR, { recursive: true }); } catch (e) {}
  postMessage({ type: 'wiped' });
}

onmessage = async ev => {
  try {
    if (ev.data.cmd === 'start') await start(ev.data.files, ev.data.version);
    else if (ev.data.cmd === 'wipe') await wipe();
  } catch (e) { postMessage({ type: 'error', message: String(e && e.message || e) }); }
};
