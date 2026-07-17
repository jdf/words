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

// Show the toolbar before measuring the canvas: it takes layout space,
// and the OffscreenCanvas is sized from the measurement below.
if (showUi) {
  document.getElementById('panel').style.display = 'flex';
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
  ['grilledcheese', 'GrilledCheese BTN'],
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

worker.onmessage = (e) => {
  const m = e.data;
  if (m.type === 'print') {
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
    // its screenshot. The idle message can outrun the worker's canvas
    // commit (separate threads), so let two compositor frames pass
    // before declaring the pixels ready.
    requestAnimationFrame(() => requestAnimationFrame(() => {
      window.__wordsIdle = true;
    }));
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

// The engine's console handle, a shim over the worker:
// words._wordsLogScene() still dumps engine state to the console.
window.words = {
  _wordsLogScene: () => worker.postMessage({ type: 'logScene' }),
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

function scatterThumb(slug) {
  // Deterministic little scatters: a wide lens for center-line, a disc
  // for center.
  const dots = [];
  const n = 26;
  for (let i = 0; i < n; ++i) {
    const t = i / (n - 1);
    const jitter = ((i * 7919) % 11 - 5) / 5;  // -1..1, fixed pattern
    let x;
    let y;
    if (slug === 'center-line') {
      x = 4 + 52 * t;
      y = 18 + jitter * 6 * Math.sin(Math.PI * (0.15 + 0.7 * t));
    } else {
      const ang = i * 2.39996;  // golden-angle spiral
      const r = 13 * Math.sqrt(t);
      x = 30 + r * Math.cos(ang) * 1.6;
      y = 18 + r * Math.sin(ang);
    }
    const rr = 1.2 + ((i * 31) % 5) / 3;
    dots.push(`<circle cx="${x.toFixed(1)}" cy="${y.toFixed(1)}"` +
              ` r="${rr.toFixed(1)}" fill="currentColor"/>`);
  }
  return `<svg class="thumb" viewBox="0 0 60 36" aria-hidden="true">${dots.join('')}</svg>`;
}

function paletteThumb(bg, colors) {
  const dots = colors.map((c, i) =>
      `<circle cx="${10 + i * 9}" cy="10" r="3.5" fill="${c}"/>`).join('');
  const w = 20 + colors.length * 9 - 9;
  return `<svg class="thumb sw" viewBox="0 0 ${w} 20" aria-hidden="true"` +
         ` style="width:${w * 1.6}px">` +
         `<rect x="0.5" y="0.5" width="${w - 1}" height="19" rx="4"` +
         ` fill="${bg}" stroke="#555"/>${dots}</svg>`;
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
      panel.hidden = false;
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
        !dialog.open) {
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
}
