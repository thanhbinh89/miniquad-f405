/**
 * Browser end-to-end check of the configurator in demo mode (the WASM
 * emulator of board v2): every page renders without errors, a short flight
 * with the virtual transmitter, a firmware update through the bootloader
 * (two USB re-enumerations), the USB cable pulled and plugged back, the
 * bootloader entered and left. Screenshots go to web/test/e2e/out/.
 *
 *   npm i -D playwright && npx playwright install chromium   (once)
 *   node test/e2e/screens.mjs                                 (from web/)
 *
 * CHROME=/usr/bin/google-chrome uses an installed Chrome instead.
 * The firmware steps need web/firmware/ filled from build/release/out.
 */
import { createReadStream, existsSync, mkdirSync, statSync } from 'node:fs';
import { createServer } from 'node:http';
import { extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const WEB = resolve(fileURLToPath(import.meta.url), '../../..');
const OUT = join(WEB, 'test/e2e/out');
mkdirSync(OUT, { recursive: true });

const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.svg': 'image/svg+xml', '.wasm': 'application/wasm', '.json': 'application/json', '.woff2': 'font/woff2', '.bin': 'application/octet-stream' };
const server = createServer((req, res) => {
  const path = join(WEB, decodeURIComponent(new URL(req.url, 'http://x').pathname));
  const file = path.endsWith('/') ? join(path, 'index.html') : path;
  if (!file.startsWith(WEB) || !existsSync(file) || !statSync(file).isFile()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': TYPES[extname(file)] || 'application/octet-stream' });
  createReadStream(file).pipe(res);
}).listen(0, '127.0.0.1');
await new Promise((r) => server.once('listening', r));
const base = `http://127.0.0.1:${server.address().port}/`;

const { chromium } = await import(process.env.MQ_PLAYWRIGHT || 'playwright');
const browser = await chromium.launch({ executablePath: process.env.CHROME || undefined, args: ['--no-sandbox'] });
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
const errors = [];
page.on('pageerror', (e) => errors.push(e.message));
page.on('console', (m) => { if (m.type() === 'error' && !/404/.test(m.text())) errors.push(m.text()); });
const shot = (name) => page.screenshot({ path: join(OUT, `${name}.png`) });
const fail = (msg) => { errors.push(msg); console.error('FAIL', msg); };

await page.goto(base);
await shot('00-connect');
await page.click('button:has-text("Board v1 · UART J2")');
await shot('00-connect-v1');
await page.click('button:has-text("Board v2 · USB-C")');
await page.click('button:has-text("Chạy mô phỏng")');
await page.waitForSelector('#rail button[data-view=tuning]:not([disabled])', { timeout: 10000 });
await page.waitForTimeout(1500);
await shot('01-dashboard');
for (const v of ['tuning', 'config', 'setup', 'firmware', 'console']) {
  await page.click(`#rail button[data-view=${v}]`);
  await page.waitForTimeout(1200);
  await shot(`02-${v}`);
}

/* fly: arm with Space, climb with W, roll/pitch with the arrows */
await page.click('#rail button[data-view=tuning]');
await page.waitForTimeout(2500);
await page.click('main h1');
await page.keyboard.press('Space');
await page.waitForTimeout(400);
await page.keyboard.down('w'); await page.waitForTimeout(900); await page.keyboard.up('w');
for (let k = 0; k < 3; k++) {
  await page.keyboard.down('ArrowRight'); await page.waitForTimeout(300); await page.keyboard.up('ArrowRight');
  await page.waitForTimeout(300);
}
const armed = await page.evaluate(() => window.mq.status?.armed);
if (!armed) fail('demo quad did not arm');
const h = await page.evaluate(() => window.mq.transport.truth().h);
if (!(h > 0.3)) fail(`demo quad did not climb (h=${h})`);
await shot('03-flight');
await page.keyboard.press('Space');
await page.waitForTimeout(2500);

/* firmware update with the images in web/firmware (built for board v2) */
if (existsSync(join(WEB, 'firmware/manifest.json'))) {
  await page.click('#rail button[data-view=firmware]');
  await page.waitForSelector('button:has-text("Dùng bản này")', { timeout: 5000 });
  await page.click('button:has-text("Dùng bản này")');
  await page.click('button:has-text("Nạp firmware")');
  await page.click('dialog button:has-text("Nạp firmware")');
  await page.waitForSelector('.fw-log li.ok', { timeout: 30000 }).catch(() => fail('firmware update did not finish'));
  const lost = await page.evaluate(() => window.mq.transport.lost ?? 0);
  await shot('04-firmware-done');
  void lost;
}

/* USB cable pulled: the page waits, then finds the board again */
await page.click('#rail button[data-view=dashboard]');
await page.waitForTimeout(800);
await page.evaluate(() => window.mq.transport.usbCable(false));
await page.waitForSelector('.callout:has-text("Mất kết nối USB")', { timeout: 3000 }).catch(() => fail('no "link lost" callout'));
await shot('05-usb-unplugged');
await page.evaluate(() => window.mq.transport.usbCable(true));
await page.waitForFunction(() => window.mq.inApp && !window.mq.linkDown, null, { timeout: 8000 }).catch(() => fail('did not reconnect after the cable came back'));
await page.waitForTimeout(800);
await shot('06-usb-back');

/* into the bootloader and back (each one a USB re-enumeration) */
await page.click('#rail button[data-view=firmware]');
await page.waitForTimeout(500);
await page.click('button:has-text("Vào bootloader")');
await page.waitForFunction(() => window.mq.hello?.mode === 0, null, { timeout: 10000 }).catch(() => fail('bootloader not reached'));
await page.waitForTimeout(500);
await shot('07-bootloader');
await page.click('button:has-text("Khởi động lại")');
await page.waitForFunction(() => window.mq.inApp, null, { timeout: 10000 }).catch(() => fail('application did not come back'));
await page.waitForTimeout(500);
await shot('08-back-in-app');

await browser.close();
server.close();
if (errors.length) {
  console.error(errors.join('\n'));
  process.exit(1);
}
console.log(`ok, screenshots in ${OUT}`);
