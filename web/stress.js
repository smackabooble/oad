const { chromium } = require('/opt/node-tools/node_modules/playwright');
(async () => {
  const url = process.argv[2], secs = parseInt(process.argv[3] || '300');
  const browser = await chromium.launch({ executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome',
    args: ['--no-sandbox','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader','--ignore-gpu-blocklist','--enable-features=WebAssemblyExperimentalJSPI','--js-flags=--experimental-wasm-jspi '+(process.env.JSX||'')] });
  const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
  const T0 = Date.now(); const ts = () => ((Date.now() - T0) / 1000).toFixed(0).padStart(4) + 's';
  let dead = false;
  let asan=false;
  page.on('console', m => { const t = m.text(); if (/Sanitizer/.test(t)) asan = true; if (asan) { console.log(ts(), t.slice(0, 260)); return; } if (/ERROR|ABORT|Assert|out of bounds|unreachable|    at /.test(t) && !/audio device|COLLADA/.test(t)) console.log(ts(), t.slice(0, 220)); });
  page.on('pageerror', e => { dead = true; console.log(ts(), '[pageerror]', (e.stack || String(e)).slice(0, 6000)); });
  await page.goto(url);
  await page.waitForTimeout(30000);
  let seed = 12345; const rnd = n => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed % n; };
  const keys = ['a','s','d','w','h','Escape','Tab','1','2','3','Control+a','F2','Space','q','e','z','x','c','v','b','n','m'];
  const t0 = Date.now();
  while (Date.now() - t0 < secs * 1000 && !dead) {
    let a = rnd(10); const allow = (process.env.ACTIONS||'move,click,right,drag,wheel,key').split(','); const name = a<3?'move':a<5?'click':a<7?'right':a<8?'drag':a<9?'wheel':'key'; if(!allow.includes(name)) { await page.waitForTimeout(150); continue; }
    try {
      if (a < 3) { await page.mouse.move(rnd(1280), rnd(720)); }
      else if (a < 5) { await page.mouse.click(rnd(1280), 40 + rnd(600)); }
      else if (a < 7) { await page.mouse.click(rnd(1280), 40 + rnd(600), { button: 'right' }); }
      else if (a < 8) { const x = rnd(1000), y = 60 + rnd(500); await page.mouse.move(x, y); await page.mouse.down(); await page.mouse.move(x + 50 + rnd(300), y + 50 + rnd(200), { steps: 5 }); await page.mouse.up(); }
      else if (a < 9) { await page.mouse.wheel(0, rnd(2) ? 300 : -300); }
      else { await page.keyboard.press(keys[rnd(keys.length)]); }
    } catch (e) { console.log(ts(), 'action failed', String(e).slice(0, 100)); break; }
    await page.waitForTimeout(150 + rnd(300));
  }
  console.log(ts(), dead ? 'CRASHED' : 'survived');
  try { await page.screenshot({ path: '/tmp/stress.png', timeout: 60000 }); } catch (e) {}
  await browser.close();
})();
