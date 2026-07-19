// The engine's home: this worker owns the wasm module and the
// OffscreenCanvas, so text analysis, shaping, layout, and rendering all
// happen off the main thread. The page (index.html) sends an init message
// with the transferred canvas, its URL query string, and the list of
// lazily fetched resources; afterwards, commands (setSeed, resize,
// logScene) dispatch to the engine's exports. The engine posts progress
// and idle messages itself (see src/main.cc); print/printErr are relayed
// here.
'use strict';

let engine = null;
const pending = [];  // commands that arrive while the module boots

function handle(msg) {
  switch (msg.type) {
    case 'rebuild': rebuild(msg); break;
    case 'resize': engine._wordsResize(msg.width, msg.height); break;
    case 'camera': engine._wordsSetCamera(msg.zoom, msg.cx, msg.cy); break;
    case 'logScene': engine._wordsLogScene(); break;
    case 'exportSvg':
      reply(msg.id,
            engine.ccall('wordsSceneSvg', 'string', ['number'],
                         [msg.background ? 1 : 0]));
      break;
    case 'exportPdf': {
      const ptr = engine.ccall('wordsScenePdf', 'number', ['number'],
                               [msg.pointWidth]);
      const size = engine._wordsScenePdfSize();
      // Copy out of the heap; the buffer is reused on the next call.
      reply(msg.id, engine.HEAPU8.slice(ptr, ptr + size));
      break;
    }
    case 'sceneSize':
      reply(msg.id, {
        width: engine._wordsSceneWidth(),
        height: engine._wordsSceneHeight(),
      });
      break;
    default: console.warn('worker: unknown message', msg);
  }
}

function reply(id, payload) {
  postMessage({ type: 'reply', id, payload });
}

// Rebuild the cloud from a spec {seed, orientation, palette, font,
// corpus, useText, text?}. Fonts and corpus TSVs are staged into MEMFS
// on first use (the page only ever names them); on a failed fetch the
// engine keeps its current font ("" path) / corpus. User text arrives
// in `text` only when it changed — once staged, `useText` alone
// selects it.
const kUserTextPath = '/user-text.txt';
const kCorpusTsvPath = '/corpus.tsv';
let stagedCorpus = null;  // slug of the TSV at kCorpusTsvPath
async function rebuild(msg) {
  let path = '/fonts/' + msg.font + '.ttf';
  if (!engine.FS.analyzePath(path).exists) {
    try {
      const r = await fetch('fonts/' + encodeURIComponent(msg.font) + '.ttf');
      if (!r.ok) throw new Error(r.status);
      engine.FS.writeFile(path, new Uint8Array(await r.arrayBuffer()));
    } catch (err) {
      postMessage({ type: 'printErr', text: 'font ' + msg.font + ': ' + err });
      path = '';
    }
  }
  if (!msg.useText && msg.corpus && msg.corpus !== stagedCorpus) {
    try {
      const r =
          await fetch('corpus/' + encodeURIComponent(msg.corpus) + '.tsv');
      if (!r.ok) throw new Error(r.status);
      engine.FS.writeFile(kCorpusTsvPath,
                          new Uint8Array(await r.arrayBuffer()));
      stagedCorpus = msg.corpus;
    } catch (err) {
      postMessage(
          { type: 'printErr', text: 'corpus ' + msg.corpus + ': ' + err });
    }
  }
  if (msg.text !== undefined) {
    engine.FS.writeFile(kUserTextPath, new TextEncoder().encode(msg.text));
  }
  engine.ccall('wordsRebuild', null,
               ['number', 'string', 'string', 'string', 'string', 'string',
                'number', 'string', 'string'],
               [msg.seed, msg.orientation, msg.placement, msg.palette, path,
                msg.useText ? kUserTextPath : '', msg.maxWords | 0,
                msg.variance || '', msg.caseFold || '']);
}

function init(msg) {
  // The engine reads URL parameters from here (a worker's own location is
  // the script URL, not the page's).
  self.__wordsSearch = msg.search;
  stagedCorpus = msg.corpus || null;  // what preRun stages below
  importScripts('words.js');
  // createWordsModule adopts this object as the Module, so the exported
  // runtime methods (FS, add/removeRunDependency) appear on it by the
  // time the preRun callback fires.
  const config = {
    canvas: msg.canvas,
    print: (text) => postMessage({ type: 'print', text }),
    printErr: (text) => postMessage({ type: 'printErr', text }),
    preRun: [() => {
      for (const f of msg.lazyFiles) {
        config.addRunDependency(f.url);
        fetch(f.url)
          .then((r) => {
            if (!r.ok) throw new Error(r.status + ' ' + f.url);
            return r.arrayBuffer();
          })
          .then((buf) => {
            config.FS.writeFile(f.path, new Uint8Array(buf));
            config.removeRunDependency(f.url);
          })
          .catch((err) => {
            postMessage({ type: 'printErr', text: 'failed to load ' + f.url + ': ' + err });
            // Release the boot anyway: the engine falls back (sample text
            // for a missing corpus, the preloaded font) rather than
            // hanging the module forever.
            config.removeRunDependency(f.url);
          });
      }
    }],
  };
  createWordsModule(config).then((module) => {
    engine = module;
    if (msg.build) {
      engine.ccall('wordsSetBuildId', null, ['string'], [msg.build]);
    }
    for (const p of pending) handle(p);
    pending.length = 0;
  }).catch((err) => {
    postMessage({ type: 'printErr', text: 'failed to start: ' + err });
  });
}

onmessage = (e) => {
  const msg = e.data;
  if (msg.type === 'init') {
    init(msg);
  } else if (engine) {
    handle(msg);
  } else {
    pending.push(msg);
  }
};
