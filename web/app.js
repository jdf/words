// The page is pure UI: the engine (wasm + WebGL) lives in a worker with an
// OffscreenCanvas, so layout never blocks this thread. The engine posts
// progress/idle; we relay prints, gate inputs, and keep the URL naming
// what's on screen.
//
// Interactivity model: the cloud spec has four style dimensions (font,
// layout, orientation, palette), each either *fixed* (locked — Generate
// keeps it) or *random* (Generate rolls it). The dropdown menus are the
// locks: choosing a specific value locks the dimension, choosing 🎲
// Random unlocks it. URL convention: `font=kenyan` is locked,
// `font=~kenyan` is random-mode currently showing kenyan — either way
// the URL reproduces the exact cloud on screen.
'use strict';

const status = document.getElementById('status');
const progress = document.getElementById('progress');
const canvas = document.getElementById('canvas');
const params = new URLSearchParams(location.search);

// The toolbar is on by default; tests (and anyone wanting a bare canvas)
// pass ?no-ui. ?ui is still accepted as a no-op.
const showUi = !params.has('no-ui');

// Show the sidebar before measuring the canvas: it takes layout space,
// and the OffscreenCanvas is sized from the measurement below. The
// with-ui class shifts the fixed status line clear of the sidebar.
if (showUi) {
  document.getElementById('panel').style.display = 'flex';
  document.body.classList.add('with-ui');
}

// ---------------------------------------------------------------------------
// The style catalog: display data mirroring the engine's tables
// (src/orientation.cc, src/layout.cc, src/palette.cc, the Basic Latin
// list in assets/fonts/capabilities.txt; family names from the fonts'
// own name tables).

const FONTS = [
  ['sexsmith', 'Sexsmith'],
  ['berylium', 'Berylium'],
  ['chunkfive', 'ChunkFive'],
  ['coolvetica', 'Coolvetica'],
  ['duality', 'Duality'],
  ['enamel', 'Enamel Brush'],
  ['exprswy_free', 'Expressway Free'],
  ['fridge', 'AlphaFridgeMagnetsAllCap'],
  ['gnuolane', 'Gnuolane'],
  ['goudy', 'Goudy Bookletter 1911'],
  ['jblack', 'JSL Blackletter'],
  ['kenyan', 'Kenyan Coffee'],
  ['king', 'Loved by the King'],
  ['leaguegothic', 'League Gothic'],
  ['mailrays', 'Mail Ray Stuff'],
  ['melochergbold', 'Meloche'],
  ['opensansbold', 'Open Sans'],
  ['owned', 'Owned'],
  ['powell', 'Powell Antique'],
  ['primerprintmedium', 'Primer Print'],
  ['steelfish', 'Steelfish'],
  ['tanklite', 'Tank'],
  ['teen', 'Teen'],
  ['telephoto', 'Telephoto'],
  ['vigo', 'Vigo'],
];

const ORIENTATIONS = [
  ['mostly-horizontal', 'Mostly Horizontal'],
  ['horizontal', 'Horizontal'],
  ['long-horizontal-likely', 'Long Horizontal Likely'],
  ['half-and-half', 'Half And Half'],
  ['mostly-vertical', 'Mostly Vertical'],
  ['vertical', 'Vertical'],
  ['any-which-way', 'Any Which Way'],
];

const PLACEMENTS = [
  ['center-line', 'Center Line'],
  ['center', 'Center'],
  ['alphabetical', 'Alphabetical'],
];

// [slug, label, background, word colors]. '' = the built-in dark scheme.
const PALETTES = [
  ['', 'App Colors', '#17171c',
   ['#EDEDE6', '#5CB8DB', '#F5A624', '#8CD44D', '#CC73C7', '#D9544F']],
  ['bw', 'BW', '#ffffff', ['#000000']],
  ['wb', 'WB', '#000000', ['#ffffff']],
  ['wordly', 'Wordly', '#ffffff',
   ['#880099', '#339922', '#993333', '#2266CC']],
  ['asparagus', 'Asparagus', '#ffffff',
   ['#CCFFCC', '#CCCC7E', '#727E4C', '#B1BD35']],
  ['bluesugar', 'BlueSugar', '#ffffff',
   ['#FFCC66', '#999999', '#9999CC', '#CCCCFF']],
  ['heat', 'Heat', '#ffffff',
   ['#CCCCCC', '#996666', '#660000', '#330000', '#666666']],
  ['ghostly', 'Ghostly', '#ffffff',
   ['#000000', '#333333', '#666666', '#888888']],
  ['chilled-summer', 'Chilled Summer', '#000000',
   ['#8DC3F2', '#CBE4F8', '#F2F2F2', '#8CBF1F', '#7AA61B']],
  ['blue-meets-orange', 'Blue Meets Orange', '#000000',
   ['#023059', '#3F7EA6', '#F2F2F2', '#D99E32', '#BF5E0A']],
  ['yramirp', 'yramirP', '#000000', ['#ff0000', '#00ff00', '#0000ff']],
];

// What 🎲 draws from. Orientation keeps a curated pool (the wilder
// strategies are opt-in); palettes exclude App Colors so Generate always
// shows an original palette.
const POOLS = {
  font: FONTS.map(([slug]) => slug),
  orientation: ['mostly-horizontal', 'horizontal', 'any-which-way'],
  placement: PLACEMENTS.map(([slug]) => slug),
  palette: PALETTES.map(([slug]) => slug).filter((s) => s !== ''),
};

const pick = (list) => list[Math.floor(Math.random() * list.length)];
const randomSeed = () => 1 + Math.floor(Math.random() * 9998);

// ---------------------------------------------------------------------------
// The cloud spec. Each style dimension carries a resolved value and a
// mode; the seed and text are always concrete. Booted from the URL:
// plain value = fixed, ~value = random-mode snapshot, absent = random
// (rolled now) in UI mode, the engine's stock defaults under ?no-ui so
// the golden harness stays deterministic.

const LEGACY_DEFAULTS = {
  font: 'sexsmith',
  orientation: 'mostly-horizontal',
  placement: 'center-line',
  palette: '',
};
// '' can't ride in a URL where "absent" means random, so App Colors gets
// a token.
const paletteToToken = (v) => (v === '' ? 'none' : v);
const tokenToPalette = (t) => (t === 'none' ? '' : t);

function bootDimension(dim) {
  const raw = params.get(dim);
  const decode = dim === 'palette' ? tokenToPalette : (x) => x;
  if (raw === null) {
    return showUi ? { mode: 'random', value: pick(POOLS[dim]) }
                  : { mode: 'fixed', value: LEGACY_DEFAULTS[dim] };
  }
  if (raw.startsWith('~')) {
    return { mode: 'random', value: decode(raw.slice(1)) };
  }
  return { mode: 'fixed', value: decode(raw) };
}

const spec = { seed: parseInt(params.get('seed'), 10) || 1447, text: '' };
for (const dim of ['font', 'orientation', 'placement', 'palette']) {
  const { mode, value } = bootDimension(dim);
  spec[dim] = value;
  spec[dim + 'Mode'] = mode;
}

// forEngine drops the ~ markers: the engine wants plain, parseable
// values, while the address bar records mode as well.
function specUrl(forEngine) {
  const url = new URL(location);
  url.searchParams.set('seed', spec.seed);
  for (const dim of ['font', 'orientation', 'placement', 'palette']) {
    const encode = dim === 'palette' ? paletteToToken : (x) => x;
    const tilde =
        !forEngine && spec[dim + 'Mode'] === 'random' ? '~' : '';
    url.searchParams.set(dim, tilde + encode(spec[dim]));
  }
  return url;
}

// In UI mode the resolved boot spec goes into the URL *before* the worker
// starts, so the engine (which reads URL parameters) and the address bar
// both name exactly what's on screen. Under ?no-ui the URL is left
// untouched.
if (showUi) {
  history.replaceState(null, '', specUrl());
}

// ---------------------------------------------------------------------------
// Worker boot.

// moby-dick is the default corpus; ?text= (or an explicit ?corpus=)
// overrides it.
const corpus =
    params.get('corpus') || (params.has('text') ? '' : 'moby-dick');
const lazyFiles = [];
if (corpus) {
  lazyFiles.push({
    url: 'corpus/' + encodeURIComponent(corpus) + '.tsv',
    path: '/corpus.tsv',
  });
}
if (spec.font !== 'sexsmith') {  // sexsmith is preloaded
  lazyFiles.push({
    url: 'fonts/' + encodeURIComponent(spec.font) + '.ttf',
    path: '/font-override.ttf',
  });
}

const dpr = window.devicePixelRatio || 1;
canvas.width = Math.round(canvas.clientWidth * dpr);
canvas.height = Math.round(canvas.clientHeight * dpr);
const offscreen = canvas.transferControlToOffscreen();

const worker = new Worker('worker.js');
worker.postMessage(
    {
      type: 'init',
      canvas: offscreen,
      search: showUi ? specUrl(true).search : location.search,
      lazyFiles,
    },
    [offscreen]);

// One command in flight at a time; the newest queued spec wins. The text
// payload (possibly megabytes) rides along only when it changed; the
// worker keeps the staged copy otherwise.
let busy = true;  // the initial build is in progress
let pendingSpec = null;
let stagedText = null;  // what the worker currently has
const sendSpec = (s) => {
  busy = true;
  window.__wordsIdle = false;
  const msg = {
    type: 'rebuild',
    seed: s.seed,
    orientation: s.orientation,
    placement: s.placement,
    palette: s.palette,
    font: s.font,
    useText: s.text !== '',
  };
  if (s.text !== '' && s.text !== stagedText) {
    msg.text = s.text;
    stagedText = s.text;
  }
  worker.postMessage(msg);
};

// Request/reply channel for export payloads (SVG text, PDF bytes, scene
// dimensions) computed by the engine.
let replySeq = 0;
const replyWaiters = new Map();
function askWorker(msg) {
  return new Promise((resolve) => {
    const id = ++replySeq;
    replyWaiters.set(id, resolve);
    worker.postMessage({ ...msg, id });
  });
}

worker.onmessage = (e) => {
  const m = e.data;
  if (m.type === 'reply') {
    const waiter = replyWaiters.get(m.id);
    if (waiter) {
      replyWaiters.delete(m.id);
      waiter(m.payload);
    }
  } else if (m.type === 'print') {
    console.log(m.text);
    status.textContent = m.text;
  } else if (m.type === 'printErr') {
    console.error(m.text);
  } else if (m.type === 'progress') {
    // Shaping is roughly the first third of the work; layout the rest.
    const frac = m.phase === 'shaping' ? 0.3 * (m.done / m.total)
                                       : 0.3 + 0.7 * (m.done / m.total);
    progress.style.opacity = 1;
    progress.style.width = (100 * frac).toFixed(1) + '%';
  } else if (m.type === 'idle') {
    busy = false;
    // The e2e harness (tools/e2e-shot.mjs) waits on this before taking
    // its screenshot. Set it directly: deferring via rAF starves in
    // headless (no frames, no rAF), and the capture itself forces a
    // composite of the worker's committed frame — the driver adds a
    // small grace period for the commit to land.
    window.__wordsIdle = true;
    progress.style.opacity = 0;
    progress.style.width = '0';
    if (pendingSpec !== null) {
      const s = pendingSpec;
      pendingSpec = null;
      sendSpec(s);
    }
  }
};

// Resize events flood during a drag; coalesce to at most one message per
// animation frame (the rAF always reads the *current* size, so the final
// geometry is never missed) to keep the worker from re-rendering dozens
// of intermediate sizes — that churn reads as flicker.
let resizeQueued = false;
window.addEventListener('resize', () => {
  if (typeof closeMenus === 'function') closeMenus();  // positions go stale
  if (resizeQueued) return;
  resizeQueued = true;
  requestAnimationFrame(() => {
    resizeQueued = false;
    worker.postMessage({
      type: 'resize',
      width: Math.round(canvas.clientWidth * dpr),
      height: Math.round(canvas.clientHeight * dpr),
    });
  });
});

// ---------------------------------------------------------------------------
// Export. Every format derives from the engine's vector output: SVG is
// served verbatim, PNG rasterizes the SVG in-page at the chosen size,
// and PDF comes from the engine with the same outlines as native path
// operators (vector, not raster).

async function rasterize(svgText, width, height, transparent) {
  // Give the SVG explicit dimensions so the browser rasterizes at
  // exactly the requested size. preserveAspectRatio="none": the target
  // is already aspect-matched to within rounding, and stretching that
  // final fraction of a pixel beats an antialiased letterbox sliver at
  // the edges.
  const sized = svgText.replace(
      '<svg ',
      `<svg width="${width}" height="${height}" preserveAspectRatio="none" `);
  const url = URL.createObjectURL(
      new Blob([sized], { type: 'image/svg+xml' }));
  try {
    const img = new Image();
    img.src = url;
    await img.decode();
    const surface = new OffscreenCanvas(width, height);
    const ctx = surface.getContext('2d');
    ctx.drawImage(img, 0, 0, width, height);
    return surface;
  } finally {
    URL.revokeObjectURL(url);
  }
}

async function buildExport(mode, opts) {
  const name = `words-${spec.seed}`;
  if (mode === 'svg') {
    const svg = await askWorker(
        { type: 'exportSvg', background: !opts.transparent });
    return {
      blob: new Blob([svg], { type: 'image/svg+xml' }),
      filename: `${name}.svg`,
    };
  }
  if (mode === 'png') {
    const svg = await askWorker(
        { type: 'exportSvg', background: !opts.transparent });
    const surface =
        await rasterize(svg, opts.width, opts.height, opts.transparent);
    return {
      blob: await surface.convertToBlob({ type: 'image/png' }),
      filename: `${name}.png`,
    };
  }
  // pdf
  const bytes = await askWorker(
      { type: 'exportPdf', pointWidth: opts.inches * 72 });
  return {
    blob: new Blob([bytes], { type: 'application/pdf' }),
    filename: `${name}.pdf`,
  };
}

function download(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 10000);
}

// The engine's console handle, a shim over the worker:
// words._wordsLogScene() still dumps engine state to the console;
// buildExport is exposed for the harness and console experiments.
window.words = {
  _wordsLogScene: () => worker.postMessage({ type: 'logScene' }),
  buildExport,
};

// ---------------------------------------------------------------------------
// Undo machinery. Every mutation flows through apply() as an action
// holding before/after spec snapshots — an object that knows how to undo
// (and redo) itself — so any change to the cloud is reversible. The
// engine is a deterministic function of the spec.

const undoStack = [];
const redoStack = [];

const submitSpec = () => {
  if (showUi) history.replaceState(null, '', specUrl());
  busy ? pendingSpec = { ...spec } : sendSpec({ ...spec });
};
const apply = (action) => {
  undoStack.push(action);
  redoStack.length = 0;
  Object.assign(spec, action.after);
  submitSpec();
  refreshUi();
};
const undo = () => {
  const action = undoStack.pop();
  if (!action) return;
  redoStack.push(action);
  Object.assign(spec, action.before);
  submitSpec();
  refreshUi();
};
const redo = () => {
  const action = redoStack.pop();
  if (!action) return;
  undoStack.push(action);
  Object.assign(spec, action.after);
  submitSpec();
  refreshUi();
};

// ---------------------------------------------------------------------------
// Toolbar.

const generateBtn = document.getElementById('generate');
const undoBtn = document.getElementById('undo');
const redoBtn = document.getElementById('redo');
const dialog = document.getElementById('words-dialog');

// ----- dropdown menus: the locks.

const MENUS = {
  font: { title: 'Font', options: FONTS },
  layout: { title: 'Layout', dim: 'placement', options: PLACEMENTS },
  orientation: { title: 'Orientation', options: ORIENTATIONS },
  palette: { title: 'Palette', options: PALETTES },
};

// Tiny inline-SVG thumbnails: word-bars at representative angles for the
// orientation strategies, dot scatters for the placements.
const BAR_THUMBS = {
  'horizontal':
      [[4, 8, 22, 0], [30, 12, 18, 0], [8, 22, 26, 0], [38, 24, 14, 0]],
  'mostly-horizontal':
      [[4, 8, 22, 0], [30, 10, 16, 0], [10, 22, 24, 0], [46, 18, 14, 90]],
  'long-horizontal-likely':
      [[4, 10, 34, 0], [46, 16, 12, 90], [8, 24, 18, 0], [30, 26, 14, 0]],
  'half-and-half':
      [[4, 8, 20, 0], [32, 16, 16, 90], [8, 22, 20, 0], [46, 20, 16, 90]],
  'mostly-vertical':
      [[10, 16, 18, 90], [22, 12, 20, 0], [36, 18, 18, 90], [48, 16, 16, 90]],
  'vertical':
      [[10, 16, 18, 90], [22, 16, 22, 90], [36, 16, 18, 90], [48, 16, 20, 90]],
  'any-which-way':
      [[6, 12, 22, 25], [28, 10, 16, -40], [12, 26, 20, 70], [38, 24, 18, -15]],
};

function barThumb(slug) {
  const rects = (BAR_THUMBS[slug] || []).map(([x, y, w, a]) => {
    const cx = x + w / 2;
    const cy = y + 2.5;
    return `<rect x="${x}" y="${y}" width="${w}" height="5" rx="2.5"` +
           ` transform="rotate(${a} ${cx} ${cy})" fill="currentColor"/>`;
  }).join('');
  return `<svg class="thumb" viewBox="0 0 60 36" aria-hidden="true">${rects}</svg>`;
}

// Word-bar silhouettes of the two cloud shapes: center-line packs a wide
// lens hugging the horizontal midline; center packs a round blob.
const PLACEMENT_THUMBS = {
  'center-line': [
    [22, 2, 10],
    [10, 9, 12], [26, 9, 16], [46, 9, 8],
    [2, 16, 13], [18, 16, 17], [38, 16, 13], [53, 16, 6],
    [9, 23, 13], [26, 23, 14], [44, 23, 9],
    [24, 30, 11],
  ],
  'center': [
    [24, 2, 13],
    [16, 9, 13], [32, 9, 13],
    [12, 16, 17], [31, 16, 18],
    [16, 23, 12], [31, 23, 14],
    [23, 30, 13],
  ],
  // The A→Z lens: same wide band as center-line, with the reading order
  // spelled out beneath.
  'alphabetical': [
    [14, 4, 12], [32, 4, 14],
    [4, 11, 15], [22, 11, 18], [44, 11, 11],
    [9, 18, 14], [27, 18, 13], [44, 18, 9],
    [17, 25, 12], [33, 25, 11],
  ],
};

function scatterThumb(slug) {
  const rects = (PLACEMENT_THUMBS[slug] || []).map(([x, y, w]) =>
      `<rect x="${x}" y="${y}" width="${w}" height="5" rx="2.5"` +
      ` fill="currentColor"/>`).join('');
  const letters = slug === 'alphabetical'
      ? `<text x="3" y="36" font-size="9" fill="currentColor"` +
        ` font-family="ui-monospace,monospace">A</text>` +
        `<text x="28" y="36" font-size="9" fill="currentColor"` +
        ` font-family="ui-monospace,monospace">M</text>` +
        `<text x="52" y="36" font-size="9" fill="currentColor"` +
        ` font-family="ui-monospace,monospace">Z</text>`
      : '';
  return `<svg class="thumb" viewBox="0 0 60 36" aria-hidden="true">${rects}${letters}</svg>`;
}

function paletteThumb(bg, colors) {
  const dots = colors.map((c, i) =>
      `<circle cx="${10 + i * 9}" cy="10" r="3.5" fill="${c}"/>`).join('');
  const w = 20 + colors.length * 9 - 9;
  // A fixed-width slot centers the variable-width swatch, so the palette
  // names line up in a clean left-aligned column.
  return `<span class="sw-slot"><svg class="thumb sw" viewBox="0 0 ${w} 20"` +
         ` aria-hidden="true" style="width:${w * 1.6}px">` +
         `<rect x="0.5" y="0.5" width="${w - 1}" height="19" rx="4"` +
         ` fill="${bg}" stroke="#555"/>${dots}</svg></span>`;
}

// Menu fonts load lazily the first time they're needed; each font option
// renders its family name in the face itself, straight from the .ttf the
// site already serves.
let menuFontsRequested = false;
function ensureMenuFonts() {
  if (menuFontsRequested) return;
  menuFontsRequested = true;
  for (const [slug] of FONTS) {
    const face = new FontFace(
        'menu-' + slug, `url(fonts/${encodeURIComponent(slug)}.ttf)`);
    face.load().then((f) => document.fonts.add(f)).catch(() => {});
  }
}

function optionContent(menuName, slug, label, extra) {
  if (menuName === 'font') {
    return `<span class="opt-label" style="font-family:'menu-${slug}',` +
           `ui-monospace,monospace;font-size:17px">${label}</span>`;
  }
  if (menuName === 'orientation') {
    return barThumb(slug) + `<span class="opt-label">${label}</span>`;
  }
  if (menuName === 'layout') {
    return scatterThumb(slug) + `<span class="opt-label">${label}</span>`;
  }
  return paletteThumb(extra[0], extra[1]) +
         `<span class="opt-label">${label}</span>`;
}

function menuLabel(menuName) {
  const menu = MENUS[menuName];
  const dim = menu.dim || menuName;
  if (spec[dim + 'Mode'] === 'random') return `🎲 Random ${menu.title}`;
  const entry = menu.options.find(([slug]) => slug === spec[dim]);
  return entry ? entry[1] : spec[dim];
}

function choose(menuName, mode, value) {
  closeMenus();
  const menu = MENUS[menuName];
  const dim = menu.dim || menuName;
  if (spec[dim + 'Mode'] === mode && spec[dim] === value) return;
  apply({
    label: mode === 'random' ? `Random ${menu.title}` : `Set ${menu.title}`,
    before: { ...spec },
    after: { ...spec, [dim]: value, [dim + 'Mode']: mode },
  });
}

function closeMenus() {
  for (const panel of document.querySelectorAll('.dd-panel')) {
    panel.hidden = true;
  }
}

function buildMenu(menuName) {
  const menu = MENUS[menuName];
  const dim = menu.dim || menuName;
  const root = document.getElementById('dd-' + menuName);
  const btn = root.querySelector('.dd-btn');
  const panel = root.querySelector('.dd-panel');

  const randomOpt = document.createElement('button');
  randomOpt.className = 'dd-opt dd-random';
  randomOpt.innerHTML =
      `<span class="opt-label">🎲 Random ${menu.title}</span>`;
  randomOpt.addEventListener('click', () =>
      choose(menuName, 'random', pick(POOLS[dim])));
  panel.appendChild(randomOpt);

  for (const [slug, label, ...extra] of menu.options) {
    const opt = document.createElement('button');
    opt.className = 'dd-opt';
    opt.dataset.slug = slug;
    opt.innerHTML = optionContent(menuName, slug, label, extra);
    opt.addEventListener('click', () => choose(menuName, 'fixed', slug));
    panel.appendChild(opt);
  }

  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    const wasHidden = panel.hidden;
    closeMenus();
    if (wasHidden) {
      if (menuName === 'font') ensureMenuFonts();
      for (const opt of panel.querySelectorAll('.dd-opt')) {
        opt.classList.toggle('selected',
            spec[dim + 'Mode'] === 'fixed' && opt.dataset.slug === spec[dim]);
      }
      panel.querySelector('.dd-random').classList.toggle('selected',
          spec[dim + 'Mode'] === 'random');
      // Fly out to the right of the sidebar, vertically centered on the
      // button but clamped to the viewport.
      panel.hidden = false;
      const btnRect = btn.getBoundingClientRect();
      const sidebar = document.getElementById('panel').getBoundingClientRect();
      const margin = 8;
      const height = panel.offsetHeight;
      const top = Math.min(
          Math.max(btnRect.top + btnRect.height / 2 - height / 2, margin),
          window.innerHeight - height - margin);
      panel.style.left = `${sidebar.right + margin}px`;
      panel.style.top = `${top}px`;
    }
  });
}

function refreshUi() {
  undoBtn.disabled = undoStack.length === 0;
  redoBtn.disabled = redoStack.length === 0;
  for (const menuName of Object.keys(MENUS)) {
    const cur = document.querySelector(`#dd-${menuName} .dd-cur`);
    cur.textContent = menuLabel(menuName);
    if (menuName === 'font') {
      if (spec.fontMode === 'fixed') {
        ensureMenuFonts();
        cur.style.fontFamily = `'menu-${spec.font}',ui-monospace,monospace`;
      } else {
        cur.style.fontFamily = '';
      }
    }
  }
}

// ----- wiring.

if (showUi) {
  for (const menuName of Object.keys(MENUS)) buildMenu(menuName);
  refreshUi();
  window.addEventListener('click', closeMenus);
  window.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') closeMenus();
  });

  // Generate: a fresh seed, plus new rolls for every unlocked dimension.
  generateBtn.addEventListener('click', () => {
    const after = { ...spec, seed: randomSeed() };
    for (const menuName of Object.keys(MENUS)) {
      const dim = MENUS[menuName].dim || menuName;
      if (spec[dim + 'Mode'] === 'random') after[dim] = pick(POOLS[dim]);
    }
    apply({ label: 'Generate', before: { ...spec }, after });
  });
  undoBtn.addEventListener('click', undo);
  redoBtn.addEventListener('click', redo);
  window.addEventListener('keydown', (e) => {
    if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'z' &&
        !dialog.open && !exportDialog.open && !creditsDialog.open) {
      e.preventDefault();
      e.shiftKey ? redo() : undo();
    }
  });

  // "Use My Words": paste or load a local file (FileReader — the file
  // never leaves the machine), then cloud it. An empty textarea returns
  // to the default corpus. Undoable like everything else.
  const useBtn = document.getElementById('use-words');
  const userText = document.getElementById('user-text');
  const userFile = document.getElementById('user-file');
  useBtn.addEventListener('click', () => {
    userText.value = spec.text;
    dialog.showModal();
  });
  userFile.addEventListener('change', () => {
    const file = userFile.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => { userText.value = reader.result; };
    reader.onerror = () => console.error('file read failed', reader.error);
    reader.readAsText(file);
  });
  document.getElementById('use-cancel').addEventListener('click', () => {
    dialog.close();
  });
  document.getElementById('use-go').addEventListener('click', () => {
    dialog.close();
    const text = userText.value.trim();
    if (text === spec.text) return;
    apply({
      label: 'Use My Words',
      before: { ...spec },
      after: { ...spec, text },
    });
  });

  // ----- Export dialog.
  const exportDialog = document.getElementById('export-dialog');
  const modeButtons = [...document.querySelectorAll('#export-mode button')];
  const sections = {
    png: document.getElementById('ex-png'),
    svg: document.getElementById('ex-svg'),
    pdf: document.getElementById('ex-pdf'),
  };
  const pngTransparent = document.getElementById('png-transparent');
  const svgTransparent = document.getElementById('svg-transparent');
  const pngSize = document.getElementById('png-size');
  const pngCustom = document.getElementById('png-custom');
  const pngW = document.getElementById('png-w');
  const pngH = document.getElementById('png-h');
  const pngDim = document.getElementById('png-dim');
  const pdfInches = document.getElementById('pdf-inches');
  const pdfDim = document.getElementById('pdf-dim');
  let exportMode = 'png';
  let aspect = 1.6;  // refreshed from the engine when the dialog opens

  const setMode = (mode) => {
    exportMode = mode;
    for (const b of modeButtons) {
      b.classList.toggle('selected', b.dataset.mode === mode);
    }
    for (const [key, el] of Object.entries(sections)) {
      el.hidden = key !== mode;
    }
  };
  for (const b of modeButtons) {
    b.addEventListener('click', () => setMode(b.dataset.mode));
  }

  const pngDims = () => {
    if (pngSize.value === 'custom') {
      return { width: Math.round(+pngW.value) || 16,
               height: Math.round(+pngH.value) || 16 };
    }
    const width = +pngSize.value;
    return { width, height: Math.round(width / aspect) };
  };
  const refreshDims = () => {
    const { width, height } = pngDims();
    pngDim.textContent =
        pngSize.value === 'custom' ? '' : `= ${width} × ${height} px`;
    const inches = +pdfInches.value || 11;
    pdfDim.textContent = `= ${inches} × ${(inches / aspect).toFixed(2)} in`;
  };

  pngSize.addEventListener('change', () => {
    const custom = pngSize.value === 'custom';
    pngCustom.hidden = !custom;
    if (custom && !pngW.value) {
      pngW.value = 1920;
      pngH.value = Math.round(1920 / aspect);
    }
    refreshDims();
  });
  // Custom dimensions stay aspect-locked: editing either edits both.
  pngW.addEventListener('input', () => {
    pngH.value = Math.round((+pngW.value || 0) / aspect) || '';
  });
  pngH.addEventListener('input', () => {
    pngW.value = Math.round((+pngH.value || 0) * aspect) || '';
  });
  pdfInches.addEventListener('input', refreshDims);

  document.getElementById('export-btn').addEventListener('click', async () => {
    const size = await askWorker({ type: 'sceneSize' });
    if (size.width > 0 && size.height > 0) {
      aspect = size.width / size.height;
    }
    if (pngSize.value === 'custom' && pngW.value) {
      pngH.value = Math.round(+pngW.value / aspect);
    }
    refreshDims();
    exportDialog.showModal();
  });
  document.getElementById('export-cancel').addEventListener('click', () => {
    exportDialog.close();
  });
  document.getElementById('export-go').addEventListener('click', async () => {
    exportDialog.close();
    const opts = exportMode === 'png'
        ? { transparent: pngTransparent.checked, ...pngDims() }
        : exportMode === 'svg'
        ? { transparent: svgTransparent.checked }
        : { inches: +pdfInches.value || 11 };
    const { blob, filename } = await buildExport(exportMode, opts);
    download(blob, filename);
  });

  // ----- Credits dialog: content/credits.md, rendered at build time and
  // fetched once.
  const creditsDialog = document.getElementById('credits-dialog');
  const creditsBody = document.getElementById('credits-body');
  let creditsLoaded = false;
  document.getElementById('credits-btn').addEventListener('click', async () => {
    if (!creditsLoaded) {
      try {
        const resp = await fetch('credits.html');
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        creditsBody.innerHTML = await resp.text();
        creditsLoaded = true;
      } catch (err) {
        console.error('credits fetch failed', err);
        creditsBody.textContent = 'Credits are unavailable.';
      }
    }
    creditsDialog.showModal();
  });
  document.getElementById('credits-close').addEventListener('click', () => {
    creditsDialog.close();
  });
}
