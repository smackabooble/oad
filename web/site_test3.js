const { chromium } = require('/opt/node-tools/node_modules/playwright');
(async () => {
  const ctx = await chromium.launchPersistentContext('/tmp/t/prof-site', {
    executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome', headless: true,
    args: ['--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader','--enable-features=WebAssemblyExperimentalJSPI','--js-flags=--experimental-wasm-jspi'] });
  const page = await ctx.newPage();
  page.on('console', m => { const t = m.text(); if (/ERROR|mainmenu|Error|GAME STARTED/.test(t)) console.log('[c]', t.slice(0,200)); });
  await page.goto('http://localhost:8096/oad/?args=-autostart=scenarios/arcadia%20-autostart-players=2');
  await page.waitForSelector('#go', { state: 'visible', timeout: 60000 });
  await page.click('#go');
  const t0 = Date.now(); let last = '';
  while (Date.now() - t0 < 900000) {
    await page.waitForTimeout(10000);
    const p = await page.textContent('#ptxt').catch(()=> '');
    if (p !== last) { console.log(p); last = p; }
    if (await page.textContent('#status').then(s => /Starting the engine/.test(s)).catch(()=>false)) break;
  }
  await page.waitForTimeout(150000);
  await page.screenshot({ path: '/tmp/t/site3.png' });
  console.log('shot');
  await ctx.close();
})();
