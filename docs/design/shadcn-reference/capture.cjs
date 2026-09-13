// Run from this directory after installing the pinned capture dependencies.
const { chromium } = require("playwright");
const path = require("node:path");
const { pathToFileURL } = require("node:url");
(async () => {
  const browser = await chromium.launch(process.env.CHROMIUM_PATH
    ? { executablePath: process.env.CHROMIUM_PATH, headless: true }
    : { headless: true });
  const page = await browser.newPage({ viewport: { width: 1280, height: 620 }, deviceScaleFactor: 1, reducedMotion: "reduce" });
  await page.goto(pathToFileURL(path.join(__dirname, "specimen.html")).href);
  await page.evaluate(() => document.fonts.ready);
  await page.screenshot({ path: path.join(__dirname, "nova-neutral-chromium.png") });
  await page.keyboard.press("Tab");
  await page.locator("#focus-").focus();
  await page.locator(".dark .cn-button-variant-default").first().hover();
  await page.screenshot({ path: path.join(__dirname, "nova-neutral-focus-hover.png") });
  await browser.close();
})().catch(error => { console.error(error); process.exitCode = 1; });
