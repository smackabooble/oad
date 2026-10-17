// Two browser pages: one hosts a match, the other joins with the game code. usage: node mp_test.js [secs] [baseUrl]
const { chromium } = require('/opt/node-tools/node_modules/playwright');
(async () => {
  const secs = parseInt(process.argv[2] || '150'); const base = process.argv[3] || 'http://localhost:8098/';
  const browser = await chromium.launch({ executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome',
    args: ['--no-sandbox','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader','--ignore-gpu-blocklist',
           '--enable-features=WebAssemblyExperimentalJSPI','--js-flags=--experimental-wasm-jspi',
           '--disable-features=WebRtcHideLocalIpsWithMdns','--allow-loopback-in-peer-connection'] });
  const T0 = Date.now(); const ts = () => ((Date.now() - T0) / 1000).toFixed(0).padStart(4) + 's';
  async function mk(name) {
    const ctx = await browser.newContext({ viewport: { width: 1024, height: 576 } });
    const page = await ctx.newPage();
    page.on('console', m => { const t = m.text(); if (!/audio device|COLLADA|FILES|Sound|SDL|Failed to load resource/.test(t) && (name==='CLIENT' || !/TIMER/.test(t))) console.log(ts(), name, t.slice(0, 200)); });
    page.on('pageerror', e => console.log(ts(), name, '[pageerror]', (e.stack || String(e)).slice(0, 5000)));
    return page;
  }
  const host = await mk('HOST'), client = await mk('CLIENT');
  await host.goto(base + '?debug&netdebug=1&signal=localhost:9000&args=-autostart=scenarios/arcadia%20-autostart-host%20-autostart-host-players=2%20-autostart-playername=Alice');
  let code = '';
  for (let i = 0; i < 120 && !code; i++) { await host.waitForTimeout(1000); code = await host.evaluate(() => window.OADNet && window.OADNet.code || '').catch(() => ''); }
  console.log(ts(), 'host code =', code);
  const ready = await host.evaluate(() => window.OADNet && window.OADNet.status).catch(() => null);
  await host.waitForTimeout(3000);
  await client.goto(base + '?debug&netdebug=1&signal=localhost:9000&args=-autostart-playername=Bob%20-autostart-client=' + code);
  await host.waitForTimeout(secs * 1000);
  console.log(ts(), 'client ui:', await client.evaluate(() => document.querySelector('#onet-msg') && document.querySelector('#onet-msg').textContent).catch(()=>null), JSON.stringify(await client.evaluate(() => window.OADNet.status).catch(()=>null)));
  console.log(ts(), 'host overlay:', await host.evaluate(() => { const m = document.querySelector('#onet-msg'); return m ? m.parentElement.style.display + ' | ' + m.parentElement.textContent : 'none'; }).catch(e => 'ERR'));
  console.log(ts(), 'host status', JSON.stringify(await host.evaluate(() => window.OADNet.status).catch(() => null)));
  for (const [name, pg] of [['host', host], ['client', client]]) {
    for (const fname of ['hs_1.bin', 'hs_1.idx', 'oos_dump.txt']) {
      const b64 = await pg.evaluate(async (fname) => {
        async function walk(dir, path, out) { for await (const [n, h] of dir.entries()) { if (h.kind === 'directory') await walk(h, path + n + '/', out); else out.push(path + n); } }
        const root = await navigator.storage.getDirectory(); const files = []; await walk(root, '', files);
        const f = files.find(p => p.endsWith('/' + fname) || p === fname);
        if (!f) return '';
        let d = root; const parts = f.split('/'); const fn = parts.pop(); for (const p of parts) d = await d.getDirectoryHandle(p);
        const buf = new Uint8Array(await (await (await d.getFileHandle(fn)).getFile()).arrayBuffer());
        let s = ''; for (let i = 0; i < buf.length; i += 32768) s += String.fromCharCode.apply(null, buf.subarray(i, i + 32768));
        return btoa(s);
      }, fname).catch(e => '');
      const out = Buffer.from(b64, 'base64'); require('fs').writeFileSync('/tmp/' + name + '_' + fname, out); console.log(ts(), name, fname, out.length);
    }
  }
  try { await host.screenshot({ path: '/tmp/mp_host.png', timeout: 60000 }); await client.screenshot({ path: '/tmp/mp_client.png', timeout: 60000 }); } catch (e) { console.log('shot failed', String(e).slice(0, 100)); }
  await browser.close();
})();
