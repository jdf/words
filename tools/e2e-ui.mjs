// e2e UI behavior tests: pin the page↔engine message routing that the
// pixel goldens can't see (they all run ?no-ui and never touch the
// toolbar) — above all that color-only spec changes (palette, variance,
// recolor seed) ride the recolor fast path instead of paying a full
// relayout, that a commit of an already-previewed change dedupes to no
// message at all, and that the one legitimately layout-changing color
// switch (App Colors ↔ palette) still takes the full rebuild. Every
// assertion is on the path taken — the engine's "recolor timings" /
// "rebuild timings" lines and progress events — never on wall-clock,
// so slow machines can't flake it. Driven by tools/e2e-ui.sh.
//
//   node tools/e2e-ui.mjs <chrome> <base-url>
import puppeteer from 'puppeteer-core';

const [chrome, baseUrl] = process.argv.slice(2);
const browser = await puppeteer.launch({
  executablePath: chrome,
  headless: true,
  args: ['--use-angle=swiftshader', '--hide-scrollbars'],
});

let failed = 0;
try {
  const page = await browser.newPage();
  await page.setViewport({ width: 1200, height: 750, deviceScaleFactor: 1 });
  // The installed-fonts case needs the Local Font Access permission,
  // which headless can only get by CDP grant.
  const cdp = await page.createCDPSession();
  await cdp.send('Browser.grantPermissions',
                 { permissions: ['localFonts'],
                   origin: new URL(baseUrl).origin });

  // Poll from this side, not via waitForFunction (see e2e-shot.mjs).
  const waitIdle = async () => {
    const deadline = Date.now() + 60000;
    while (!(await page.evaluate('window.__wordsIdle === true'))) {
      if (Date.now() > deadline) throw new Error('idle timeout');
      await new Promise((r) => setTimeout(r, 100));
    }
  };

  // Run `action` in the page, wait for the engine to settle, and check
  // the worker messages it produced: `path` names the build path that
  // must appear ('recolor' | 'rebuild' | 'none'); progress events are
  // forbidden unless the path is a real rebuild.
  const step = async (name, action, path) => {
    const mark = await page.evaluate('window.__uiEvts.length');
    await page.evaluate(action);
    if (path === 'none') {
      await new Promise((r) => setTimeout(r, 400));  // nothing to wait on
    } else {
      await new Promise((r) => setTimeout(r, 50));
      await waitIdle();
    }
    const evts = await page.evaluate((m) => window.__uiEvts.slice(m), mark);
    const got = evts.some((e) => /rebuild timings/.test(e)) ? 'rebuild'
              : evts.some((e) => /recolor timings/.test(e)) ? 'recolor'
              : 'none';
    const progress = evts.filter((e) => e === 'progress').length;
    const ok = got === path &&
        (path === 'rebuild' ? progress > 0 : progress === 0);
    console.log(`${ok ? 'e2e ui OK' : 'e2e ui FAIL'}: ${name}` +
                (ok ? '' : ` (path ${got}, ${progress} progress events;` +
                           ` events ${JSON.stringify(evts)})`));
    if (!ok) failed = 1;
  };

  const check = async (name, expr) => {
    const ok = await page.evaluate(expr);
    console.log(`${ok ? 'e2e ui OK' : 'e2e ui FAIL'}: ${name}`);
    if (!ok) failed = 1;
  };

  // A fully locked UI-mode boot; a small cloud for speed; sexsmith is
  // the preloaded font, so there is no font fetch to wait on.
  // Record every worker→page message; timing messages keep their text
  // (that's where "recolor timings" vs "rebuild timings" shows up).
  // Re-run after any goto — a fresh page has no instrumentation.
  const instrument = () => page.evaluate(() => {
    window.__uiEvts = [];
    const orig = worker.onmessage;
    worker.onmessage = (e) => {
      window.__uiEvts.push(
          e.data.type === 'timing' ? e.data.text : e.data.type);
      orig(e);
    };
  });

  await page.goto(
      `${baseUrl}?corpus=moby-dick&seed=7&max=300&font=sexsmith` +
          '&orientation=mostly-horizontal&placement=center-line' +
          '&palette=wordly&variance=little',
      { waitUntil: 'load' });
  await waitIdle();
  await instrument();

  // The FIRST post-boot change: the boot spec primes the routing
  // comparison, so even this must recolor. (Regression: a full rebuild
  // with a progress bar on the first variance nudge.)
  await step('first variance drag step previews via recolor', () => {
    const slider = document.querySelector('#variance-inline .var-input');
    slider.value = 4;
    slider.dispatchEvent(new Event('input'));
  }, 'recolor');

  await step('variance release commits with no rebuild at all (deduped)',
      () => {
        document.querySelector('#variance-inline .var-input')
            .dispatchEvent(new Event('change'));
      }, 'none');
  await check('variance commit landed in the spec and undo stack',
      () => spec.variance === 'wild' && undoStack.length === 1);

  await step('palette menu switch recolors',
      () => choose('palette', 'fixed', 'bw'), 'recolor');
  await check('status line names the new palette',
      () => document.getElementById('status').textContent
                .includes(' BW '));

  await step('Recolor button recolors', () => {
    document.getElementById('recolor').click();
  }, 'recolor');

  await step('undo of a color-only change recolors', () => undo(),
      'recolor');

  await step('palette editor preview recolors', () => {
    openPaletteEditor();
    document.querySelector('#pal-colors .pal-add').click();
  }, 'recolor');

  await step('palette editor cancel restores via recolor', () => {
    document.getElementById('pal-cancel').click();
  }, 'recolor');
  await check('cancel left the committed palette in the status line',
      () => document.getElementById('status').textContent
                .includes(' BW '));

  await step('App Colors crossing takes the full rebuild', () =>
      choose('palette', 'fixed', ''), 'rebuild');

  // Looseness: a spiral-step change is a relayout — full rebuild — and
  // the slider value rides the spec and the copy link.
  await step('looseness change relayouts (full rebuild)', () => {
    const slider = document.querySelector('#loose-inline .loose-input');
    slider.value = 3;
    slider.dispatchEvent(new Event('change'));
  }, 'rebuild');
  await check('looseness landed in spec and the copy link',
      () => spec.loose === 3 &&
            specUrl().searchParams.get('loose') === '3');

  // The curated first cloud: a bare boot (no style params) opens with
  // the aspect-fitting placement (1200x750 is wide → center-line),
  // mostly-horizontal, and Blue Meets Orange — each still in random
  // mode, so Generate rolls freely afterwards.
  await page.goto(`${baseUrl}?corpus=moby-dick&max=300&seed=7`,
                  { waitUntil: 'load' });
  await waitIdle();
  await check('bare boot curates the first cloud (random mode kept)',
      () => spec.placement === 'center-line' &&
            spec.placementMode === 'random' &&
            spec.orientation === 'mostly-horizontal' &&
            spec.orientationMode === 'random' &&
            spec.palette === 'blue-meets-orange' &&
            spec.paletteMode === 'random');

  // Use My Font: adopting a font file (goudy's bytes standing in for a
  // user's own) must rebuild with the user font — the status line names
  // the family the ENGINE read from the staged bytes — and hide Copy
  // Link (a local-font cloud isn't URL-reproducible).
  await instrument();
  await step('a local font file rebuilds with the user font', async () => {
    const bytes = await (await fetch('fonts/goudy.ttf')).arrayBuffer();
    await useLocalFont('MyOwnFont.ttf', bytes);
  }, 'rebuild');
  await check('the engine shaped with the local font; Copy Link hid',
      () => spec.font.startsWith('local:') &&
            document.getElementById('status').textContent
                .includes('Goudy Bookletter 1911') &&
            document.getElementById('copy-link').hidden);
  await check('an invalid font file is rejected without applying',
      async () => !(await useLocalFont('fake.ttf', new ArrayBuffer(8))) &&
                  spec.font.startsWith('local:1'));

  // Installed fonts: the picker lists the machine's families (whatever
  // they are — assert mechanism, not names) and adopting one rebuilds
  // through the same local-font path.
  await step('an installed font adopts and rebuilds', async () => {
    await openSystemFonts();
    if (!sysFontDialog.open || !sysFontFamilies.length) {
      throw new Error('installed-font picker failed to open');
    }
    const pick = ['Arial', 'Helvetica', 'Georgia', 'DejaVu Sans']
        .find((n) => sysFontFamilies.some((f) => f.family === n)) ||
        sysFontFamilies[0].family;
    sysFontSearch.value = pick;
    sysFontSearch.dispatchEvent(new Event('input'));
    const row = [...sysFontList.children]
        .find((r) => r.textContent === pick) || sysFontList.children[0];
    row.click();  // blob → FontFace → apply, all async
    const t0 = Date.now();
    while (!(spec.font.startsWith('local:') && spec.font !== 'local:1')) {
      if (Date.now() - t0 > 15000) throw new Error('adoption timed out');
      await new Promise((r) => setTimeout(r, 50));
    }
  }, 'rebuild');
  await check('the installed font is the fixed spec font',
      () => spec.fontMode === 'fixed' && !sysFontDialog.open);
} finally {
  await browser.close();
}
process.exit(failed);
