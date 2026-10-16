const { chromium } = require('/opt/node-tools/node_modules/playwright');
(async () => {
  const b = await chromium.launch({ executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome',
    args: ['--no-sandbox','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader','--enable-features=WebAssemblyExperimentalJSPI','--js-flags=--experimental-wasm-jspi'] });
  const page = await b.newPage({ viewport: { width: 1280, height: 720 } });
  page.on('console', m => { const t = m.text(); if (/ERROR|ABORT|proxy_async|Assert/.test(t) && !/audio device|COLLADA/.test(t)) console.log('[c]', t.slice(0,160)); });
  await page.goto(process.argv[2]);
  const T = parseInt(process.argv[3]||'600');
  for (let t = 0; t < T; t += 30) {
    await page.waitForTimeout(30000);
    const m = await page.evaluate(() => (typeof wasmMemory !== 'undefined') ? Math.round(wasmMemory.buffer.byteLength/1048576) : -1).catch(()=>-2);
    console.log((t+30)+'s wasm='+m+'MB');
  }
  await b.close();
})();
