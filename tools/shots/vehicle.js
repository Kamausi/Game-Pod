// Vehicle portraits with the free camera: node vehicle.js body tire x z name:dx,dy,dz,tx,ty,tz,fov ...
// (camera offsets are relative to the vehicle's warp point; heights above ground)
const { chromium } = require('playwright-core');
(async () => {
  const [body, tire, wx, wz, ...shots] = process.argv.slice(2);
  const browser = await chromium.launch({ executablePath: process.env.CHROME || '/opt/pw-browsers/chromium-1194/chrome-linux/chrome',
    args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const page = await browser.newPage({ viewport: { width: 960, height: 540 } });
  const errs = []; page.on('pageerror', (e) => errs.push(e.message));
  await page.goto(process.env.GAME_URL || 'http://localhost:8787/', { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => window.Module && Module._mb_hud && document.getElementById('loading').classList.contains('hide'), null, { timeout: 300000 });
  const act = (a) => page.evaluate((a) => Module.ccall('mb_action', null, ['string'], [a]), a);
  await page.evaluate(([b, t, process_cfg]) => { document.getElementById('s_career').checked = false; cfg.body = b; cfg.tire = +t; Object.assign(cfg, JSON.parse(process_cfg)); applyCfg(); api.resize(960, 540); startSolo(0); }, [body, tire, process.env.CFG || '{}']);
  await page.waitForTimeout(2500);
  await page.evaluate(() => { for (const id of ['hud', 'announce', 'coach']) { const e = document.getElementById(id); if (e) e.style.display = 'none'; } });
  await act(`warp:${wx},${wz},0`);
  await page.waitForTimeout(4000);
  for (const s of shots) {
    const [name, c] = s.split(':');
    const v = c.split(',').map(Number);
    await act(`freecam:${+wx + v[0]},${v[1]},${+wz + v[2]},${+wx + v[3]},${v[4]},${+wz + v[5]},${v[6] || 45},1`);
    await page.waitForTimeout(4000);
    await page.screenshot({ path: `${process.env.SHOTS_DIR || 'shots'}/veh_${name}.png`, timeout: 180000 });
  }
  console.log('errors', JSON.stringify(errs));
  await browser.close();
})();
