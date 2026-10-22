#!/usr/bin/env node
// Mide fps y ciclos por frame en tiempo EMULADO con el contador de ciclos del periférico
// de depuración 0xB7E928 (cpucycleunit=256 según WinUAE), independiente del ancho de banda host.
// La ventana de medida empieza cuando el run-status confirma READY; el tiempo de arranque
// variable (assets/init) no se mezcla con el rendimiento del bucle de frames.
//
// Uso:
//   node tools/debug/measure-fps.mjs <demo_dir_name> [CONFIG_NAME] [--json] [--duration-ms N]
//        [--require-stable]   (sale con código 3 si la cadencia no es estable o cuantiza)
//
// --json imprime, como última línea, un objeto JSON con el resultado (demo, config,
// fecha, commit, fps emulado/host, ciclos/frame y detail) para consumo por tooling
// (p. ej. tools/debug/record-fps.mjs).
//
// Puertos: WINUAE_GDB_PORT (GDB, 2345) y WINUAE_SIDE_CHANNEL_PORT (lateral, 2346).
// Parametrizables para convivir con otras instancias de WinUAE (no pisar la ajena).
//
// La dirección runtime de `g_eng_run_status` se resuelve por el `.map` (símbolo +
// secciones) en vez de escanear el magic ENG: el escaneo daba falsos positivos.
import { WinUAEConnection } from '../../../mcp-winuae-emu/dist/winuae-connection.js';
import { sideChannelCommand } from '../../../mcp-winuae-emu/dist/side-channel.js';
import { execSync } from 'node:child_process';
import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');

const ARGV = process.argv.slice(2);
const JSON_OUT = ARGV.includes('--json');
const durationArg = ARGV.indexOf('--duration-ms');
const DURATION_MS = durationArg >= 0
  ? Math.max(1000, parseInt(ARGV[durationArg + 1] || '20000', 10))
  : 20000;
const POSITIONAL = ARGV.filter((a) => !a.startsWith('-'));
const DEMO = POSITIONAL[0];
if (!DEMO) { console.error('Uso: node tools/debug/measure-fps.mjs <demo_dir_name> [CONFIG_NAME] [--json]'); process.exit(1); }
const CONFIG_NAME = POSITIONAL[1] || 'A500_debug';
const GDB_PORT = parseInt(process.env.WINUAE_GDB_PORT || '2345', 10);
const SIDE_PORT = parseInt(process.env.WINUAE_SIDE_CHANNEL_PORT || '2346', 10);
const MAP = `${ROOT}/out/demos/${DEMO}/${CONFIG_NAME}/${DEMO}.${CONFIG_NAME}.map`;
// WinUAE + dh0 de la extension Bartman: se autodetecta la version MAS NUEVA instalada (usar una
// fija desincronizaba con run-demo/launch-winuae y podia colgar el arranque de algunas demos).
const EXT_BIN = (() => {
  const base = `${process.env.USERPROFILE || process.env.HOME || ''}/.vscode/extensions`;
  try {
    const dirs = fs.readdirSync(base).filter((d) => d.startsWith('bartmanabyss.amiga-debug-')).sort();
    if (dirs.length) return `${base}/${dirs[dirs.length - 1]}/bin`;
  } catch { /* fallback */ }
  return `${base}/bartmanabyss.amiga-debug-1.8.2/bin`;
})();
const DH0 = `${EXT_BIN}/dh0`;
fs.mkdirSync(DH0 + '/s', { recursive: true });
fs.writeFileSync(DH0 + '/s/startup-sequence', 'stack 131072\ncd dh1:\n:a.exe\n', 'utf8');

const CONFIG = {
  // `AMIGA_WINUAE_PATH` fuerza el emulador (mismo contrato que winuae-profile.mjs):
  // en este workspace apunta al fork `../WinUAE-DBG/bin` (side-channel + puerto GDB
  // configurable); sin la variable se usa el WinUAE stock de la extension.
  winuaePath: (process.env.AMIGA_WINUAE_PATH || `${EXT_BIN}/win32`).replace(/\\/g, '/'),
  configFile: `${ROOT}/out/run/${DEMO}/${CONFIG_NAME}/runner.uae`,
  gdbPort: GDB_PORT,
};
const CPU_HZ = 7093790;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function findMapSymbol(name) {
  if (!fs.existsSync(MAP)) return null;
  const re = new RegExp('^\\s*0x([0-9a-fA-F]+)\\s+' + name + '\\b');
  for (const line of fs.readFileSync(MAP, 'utf8').split(/\r?\n/g)) {
    const m = line.match(re);
    if (m) return parseInt(m[1], 16);
  }
  return null;
}
function mapSections() {
  const wanted = new Set(['.text', '.rodata', '.eh_frame', '.data', '.bss']);
  const out = [];
  for (const line of fs.readFileSync(MAP, 'utf8').split(/\r?\n/g)) {
    const m = line.match(/^(\.[A-Za-z0-9_.]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)/);
    if (!m || !wanted.has(m[1])) continue;
    const start = parseInt(m[2], 16), size = parseInt(m[3], 16);
    if (size === 0) continue;
    out.push({ start, size, end: start + size });
  }
  return out;
}
function runtimeAddr(linked, ms, rs) {
  if (!Array.isArray(rs) || rs.length === 0) return null;
  const cand = ms.find((s) => linked >= s.start && linked < s.end);
  if (!cand) return parseInt(rs[0], 16) + (linked - 0x400);
  const idx = ms.indexOf(cand);
  if (rs.length <= idx) return null;
  return parseInt(rs[idx], 16) + (linked - cand.start);
}

const RUN_STATUS_MAGIC = '0x454e4752';

/// Resuelve la direccion runtime de `g_eng_run_status` de forma robusta. Candidatos, en orden,
/// validados con `runstatus` (magic + `version === 1`):
///   1) mapeo por indice (seccion `.map` -> seccion runtime);
///   2) **mismo offset dentro de CADA seccion runtime** (el numero/orden de hunks runtime no
///      casa con el `.map`: p. ej. `.rodata`/`.data` desplazadas por assets o un `.s` de support);
///   3) inicio de cada seccion runtime;
///   4) escaneo amplio de memoria buscando el magic big-endian (ENGR).
/// Devuelve `null` si ningun candidato valida. El check `version === 1` descarta falsos
/// positivos del escaneo (bytes "ENGR" en codigo/strings).
async function resolveRunStatusAddr(linked, ms, rs) {
  const ok = async (addr) => {
    if (!addr) return false;
    try {
      const r = await sideChannelCommand('runstatus 0x' + addr.toString(16), SIDE_PORT, 2000);
      const v = r.reply;
      return !!(v && v.magic === RUN_STATUS_MAGIC && v.version === 1);
    } catch {
      return false;
    }
  };
  if (!Array.isArray(rs) || rs.length === 0) return null;
  const cand = ms.find((s) => linked >= s.start && linked < s.end);
  const off = cand ? (linked - cand.start) : (linked - 0x400);
  // 1) Mapeo por indice (seccion del `.map` -> seccion runtime).
  const idx = cand ? ms.indexOf(cand) : -1;
  if (cand && rs.length > idx) {
    const a = parseInt(rs[idx], 16) + off;
    if (await ok(a)) return a;
  }
  // 2) MISMO offset dentro de CADA seccion runtime: el numero/orden de hunks runtime no casa con
  //    el `.map` (p. ej. `.rodata` partida por assets), asi que se prueba cada base con el offset
  //    que el simbolo tiene en su seccion del `.map`.
  for (const sec of rs) {
    const a = parseInt(sec, 16) + off;
    if (await ok(a)) return a;
  }
  // 3) Inicio de cada seccion runtime.
  for (const sec of rs) {
    const a = parseInt(sec, 16);
    if (a && await ok(a)) return a;
  }
  // 3) Escaneo AMPLIO de memoria (de la 1.ª a la última sección + margen): el símbolo no cae
  //    al inicio de un hunk y los hunks runtime no casan con el `.map` (p. ej. `.data` tras
  //    `.rodata` con assets). Se busca el magic big-endian (ENGR) y se valida con `runstatus`.
  if (rs.length) {
    const lo = parseInt(rs[0], 16);
    const hi = parseInt(rs[rs.length - 1], 16) + 0x8000;
    for (let base = lo; base < hi; base += 0x1000) {
      try {
        const r = await sideChannelCommand('mem 0x' + base.toString(16) + ' 4096', SIDE_PORT, 2500);
        const hex = (r && r.reply && typeof r.reply.data === 'string') ? r.reply.data : null;
        if (!hex) continue;
        for (let from = 0; ; from += 2) {
          const j = hex.indexOf('454e4752', from);
          if (j < 0) break;
          if ((j & 1) === 0) {
            const candidate = base + (j >> 1);
            if (await ok(candidate)) return candidate;
          }
        }
      } catch { /* noop */ }
    }
  }
  return null;
}

// La medida exige que el `a.exe` montado en `dh1` sea EXACTAMENTE la build cuyo
// `.map` usamos para resolver simbolos. Si no coincide, la direccion de
// `g_eng_run_status` cae en otro sitio y la medida sale invalida (detail=0x0,
// contador de frames retrocediendo). Por eso copiamos aqui la build recien
// compilada; no dependemos de un `run-demo` previo.
const RUN_DIR = `${ROOT}/out/run/${DEMO}/${CONFIG_NAME}`;
const RUNNER_UAE = `${RUN_DIR}/runner.uae`;
const BUILT_EXE = `${ROOT}/out/demos/${DEMO}/${CONFIG_NAME}/${DEMO}.${CONFIG_NAME}.exe`;
if (!fs.existsSync(RUNNER_UAE)) {
  console.error(`[fps] falta ${RUNNER_UAE}; ejecuta una vez 'bash tools/run/run-demo.sh demos/amiga/${DEMO}' para generarlo.`);
  process.exit(1);
}
if (!fs.existsSync(BUILT_EXE)) {
  console.error(`[fps] falta ${BUILT_EXE}; compila la demo: 'bash tools/build/build-demo.sh demos/amiga/${DEMO}${CONFIG_NAME === 'A500_release' ? ' --release' : ''}'.`);
  process.exit(1);
}
const STAGED_DIR = `${RUN_DIR}/dh1`;
fs.mkdirSync(STAGED_DIR, { recursive: true });
fs.copyFileSync(BUILT_EXE, `${STAGED_DIR}/a.exe`);
console.log(`[fps] staged ${path.basename(BUILT_EXE)} -> ${STAGED_DIR}/a.exe`);

const conn = new WinUAEConnection(CONFIG);
await conn.connect({ forceBreak: false, initializeStopped: true });
const p = conn.getProtocol();
await p.continue();
// El Shell del runner tarda en cargar `a.exe` después del continue; esperar a que existan sus
// secciones runtime para resolver el símbolo no forma parte de la ventana medida. READY se
// espera explícitamente más abajo, así que esta espera solo separa carga de medición.
await sleep(10000);

const st = await sideChannelCommand('state', SIDE_PORT, 5000);
const linked = findMapSymbol('g_eng_run_status');
const magicAddr = linked !== null ? await resolveRunStatusAddr(linked, mapSections(), st.reply.sections) : null;
console.log('[fps] map=' + MAP + ' linked=' + (linked ?? 'null') + ' runtime=0x' + (magicAddr ?? 0).toString(16));
if (!magicAddr) {
  console.log('[fps] no se pudo resolver g_eng_run_status (¿map? ¿sections?)');
  await conn.disconnect(true);
  process.exit(1);
}
const state = async () => {
  // Contador de ciclos del periférico de depuración ($B7E928, unidades de `CPU_HZ`) leído
  // por el **canal lateral**. Un `readMemory` GDB puede detener/reanudar el CPU y perturbar
  // el latido VERTB: la build de 4 planos de la 224 se congelaba solo bajo la medida por GDB
  // y corría estable sin cliente GDB. El canal lateral es no intrusivo; se mantienen los
  // reintentos por si la sesión queda atascada (DT-025).
  let cycles = null;
  let lastErr = null;
  for (let attempt = 1; attempt <= 5 && cycles === null; ++attempt) {
    try {
      const r = await sideChannelCommand('mem b7e928 4', SIDE_PORT, 3000);
      const hex = r?.reply?.data;
      if (typeof hex === 'string' && hex.length === 8) {
        cycles = parseInt(hex, 16) >>> 0;
      } else {
        throw new Error('respuesta sin datos');
      }
    } catch (err) {
      lastErr = err;
      await sleep(500);
    }
  }
  if (cycles === null) {
    throw new Error(
      `no se pudo leer el contador de ciclos en $b7e928 tras 5 intentos (${lastErr?.message ?? 'desconocido'}); ` +
        'el canal lateral puede estar atascado (DT-025): reintenta la medida o reinicia la instancia.',
    );
  }
  const r = await sideChannelCommand('runstatus 0x' + magicAddr.toString(16), SIDE_PORT, 3000);
  return { cycles, frame: r.reply.frame, detail: r.reply.detail, runState: r.reply.state };
};

// No medir la inicialización: en escenas cargadas puede durar más de 10 s. Esperar el estado
// READY explícito evita contar ventanas sin juego (o declarar un frame rate ficticio por el
// arranque). El polling solo lee el estado lateral; no detiene el 68000 ni solicita capturas.
const RUN_STATE_READY = 3;
const RUN_STATE_FAILED = 4;
const readyDeadline = Date.now() + 60000;
let readyStatus = null;
while (Date.now() < readyDeadline) {
  try {
    const r = await sideChannelCommand('runstatus 0x' + magicAddr.toString(16), SIDE_PORT, 3000);
    readyStatus = r.reply;
  } catch {
    readyStatus = null;
  }
  if (readyStatus?.state === RUN_STATE_READY) break;
  if (readyStatus?.state === RUN_STATE_FAILED) {
    console.error('[fps] la demo terminó en FAILED antes de READY (detail=0x' +
      (readyStatus.detail >>> 0).toString(16) + ')');
    await conn.disconnect(true);
    process.exit(2);
  }
  await sleep(250);
}
if (readyStatus?.state !== RUN_STATE_READY) {
  console.error('[fps] timeout esperando READY (60 s; detail=0x' +
    ((readyStatus?.detail ?? 0) >>> 0).toString(16) + ')');
  await conn.disconnect(true);
  process.exit(2);
}
console.log(`[fps] READY en frame=${readyStatus.frame}; comienza la ventana de medida`);
// **Muestreo por sub-ventanas**: una media única engaña cuando el frame está en el borde
// del campo (una demo que alterna frames de 1 y 2 campos da ~33 fps de media, pero ni va a
// 50 ni a 25: cada ventana oscila). Se toman N sub-ventanas, se calcula el fps de cada una
// y se informa min/max: si la dispersión es alta, la cadencia es inestable y la media NO es
// un dato válido para el presupuesto (medido en la 302: el frame a ~142k de 142.102 alternaba
// 1/2 campos y `measure-fps` daba 26, 33 o 49 según la fase de la ventana).
const SAMPLE_COUNT = 8;
const sampleMs = Math.max(500, Math.floor(DURATION_MS / SAMPLE_COUNT));
const samples = [];
let t0 = Date.now();
let prev = await state();
for (let i = 0; i < SAMPLE_COUNT; ++i) {
  await sleep(sampleMs);
  const now = await state();
  let dC = now.cycles - prev.cycles;
  if (dC < 0) dC += 4294967296;
  const dF = now.frame - prev.frame;
  samples.push({
    frames: dF,
    cycles: dC,
    fps: dC > 0 ? dF / (dC / CPU_HZ) : 0,
    detail: now.detail,
  });
  prev = now;
}
const t1 = Date.now();
// Si NINGUNA sub-ventana avanzó frames, el run-status no está vivo (dirección falsa o demo
// colgada): reintento corto antes de abortar, con diagnóstico (antes se abortaba a ciegas).
if (samples.every((s) => s.frames <= 0)) {
  console.error('[fps] ninguna sub-ventana avanzó frames (run-status sin actualizar en 0x' +
    magicAddr.toString(16) + ', detail=0x' + (samples[samples.length - 1].detail >>> 0).toString(16) +
    '): reintento en 3 s…');
  // Diagnóstico del latido: distingue "la OS retiró VERTB" (DT-011/212) de "VERTB pendiente
  // pero la CPU no la atiende" (depurador detenido / VPOSR latcheado por el lightpen).
  try {
    const irq = await sideChannelCommand('mem dff01c 4', SIDE_PORT, 3000);
    const hex = irq?.reply?.data;
    if (typeof hex === 'string' && hex.length === 8) {
      const intena = parseInt(hex.slice(0, 4), 16);
      const intreq = parseInt(hex.slice(4, 8), 16);
      const vblOn = (intena & 0x0020) !== 0;
      const vblPending = (intreq & 0x0020) !== 0;
      console.error(`[fps] diagnóstico IRQ: INTENAR=0x${intena.toString(16).padStart(4, '0')} INTREQR=0x${intreq.toString(16).padStart(4, '0')} (VERTB ${vblOn ? 'habilitada' : 'deshabilitada'}, ${vblPending ? 'pendiente' : 'sin petición'})`);
      if (vblPending && !vblOn) {
        console.error('[fps] la OS/exec ha retirado VERTB de INTENA (familia 212/DT-011): usa takeover + AddIntServer o VblankWait::Polling.');
      } else if (vblPending && vblOn) {
        console.error('[fps] VERTB pendiente con enable: la CPU no atiende el nivel 3 (depurador detenido o VPOSR latcheado; ver DT-045). Desconecta sesiones MCP/GDB ajenas.');
      }
    }
  } catch {
    // diagnóstico best-effort
  }
  await sleep(3000);
  const retry = await state();
  if (retry.frame - prev.frame <= 0) {
    console.error('[fps] sigue sin avanzar: demo colgada o símbolo `g_eng_run_status` incorrecto; medida abortada.');
    await conn.disconnect(true);
    process.exit(2);
  }
}
const dFrame = samples.reduce((s, x) => s + x.frames, 0);
const dCycles = samples.reduce((s, x) => s + x.cycles, 0);
const emuFps = dCycles > 0 ? dFrame / (dCycles / CPU_HZ) : 0;
const hostFps = dFrame / ((t1 - t0) / 1000);
const cyclesPerFrame = dCycles / Math.max(1, dFrame);
const nominalFieldsPerFrame = cyclesPerFrame / (CPU_HZ / 50);
const fpsList = samples.map((s) => s.fps);
const minFps = Math.min(...fpsList);
const maxFps = Math.max(...fpsList);
const spread = maxFps - minFps;
const stable = spread <= Math.max(2, 0.1 * emuFps);
// **Detección de cuantización por campos**: un frame cuyo trabajo excede un campo por poco
// cae deterministamente a 1 frame cada N campos (50/N fps: 25,0 / 16,7 / 12,5). Es el modo de
// fallo más silencioso del presupuesto (¡parece «estable»!), así que se detecta y se avisa
// aunque `stable` sea true. Se miran las ventanas tras el arranque (las 2 primeras incluyen
// init y no representan el régimen).
const core = samples.slice(2).filter((s) => s.frames > 0).map((s) => s.fps).sort((a, b) => a - b);
let quantizedFields = 0;
if (core.length >= 3) {
  const median = core[core.length >> 1];
  for (let n = 1; n <= 4; n++) {
    const nominal = 50 / n;
    if (Math.abs(median - nominal) <= Math.max(1.0, 0.05 * nominal)) { quantizedFields = n; break; }
  }
}
console.log('[fps] ' + DEMO + '/' + CONFIG_NAME +
  ' | emulado=' + emuFps.toFixed(2) + ' fps' +
  ' | host=' + hostFps.toFixed(2) + ' fps' +
  ' | ' + cyclesPerFrame.toFixed(0) + ' ciclos/frame (' + nominalFieldsPerFrame.toFixed(3) + ' campos/frame)' +
  ' | por ventana=' + fpsList.map((f) => f.toFixed(1)).join('/') +
  ' | cuantizacion=' + (quantizedFields || '?') + ' campos/frame' +
  ' | detail=0x' + (samples[samples.length - 1].detail >>> 0).toString(16));
if (!stable) {
  console.error('[fps] AVISO: cadencia INESTABLE (min=' + minFps.toFixed(2) + ' max=' + maxFps.toFixed(2) +
    ' fps; dispersión ' + spread.toFixed(2) + '): el frame está en el borde del campo (alterna 1 y 2 campos)' +
    ' o hay picos periódicos. La media (' + emuFps.toFixed(2) + ') NO vale como presupuesto: perfila por' +
    ' secciones (`ENG_PROF_*`) y mira el peor caso.');
}
if (quantizedFields > 1) {
  console.error('[fps] AVISO: CUANTIZACIÓN a ' + quantizedFields + ' campos/frame (~' + (50 / quantizedFields).toFixed(1) +
    ' fps estables): el frame excede el presupuesto de 1 campo por poco y cae a 1/N determinista (la media parece' +
    ' «estable»). Recorta trabajo del frame (p. ej. HUD por diferencias con `DiffText`, ablaciones `ENG_PROF`) y' +
    ' vuelve a medir.');
}

if (JSON_OUT) {
  let commit = null;
  try { commit = execSync('git rev-parse --short HEAD', { cwd: ROOT }).toString().trim(); } catch { /* sin git */ }
  const result = {
    demo: DEMO,
    config: CONFIG_NAME,
    date: new Date().toISOString().slice(0, 10),
    commit,
    emulatedFps: Number(emuFps.toFixed(2)),
    hostFps: Number(hostFps.toFixed(2)),
    cyclesPerFrame: Math.round(cyclesPerFrame),
    fieldsPerFrame: Number(nominalFieldsPerFrame.toFixed(5)),
    frames: dFrame,
    cycles: dCycles,
    measurementMs: t1 - t0,
    detail: '0x' + (samples[samples.length - 1].detail >>> 0).toString(16),
    stable,
    quantizedFields: quantizedFields || null,
    minFps: Number(minFps.toFixed(2)),
    maxFps: Number(maxFps.toFixed(2)),
    samples: samples.map((s) => ({ frames: s.frames, fps: Number(s.fps.toFixed(2)) })),
  };
  console.log(JSON.stringify(result));
}

if (ARGV.includes('--require-stable') && (!stable || quantizedFields > 1)) {
  console.error('[fps] --require-stable: la medida no cumple (stable=' + stable +
    ', cuantizacion=' + (quantizedFields || '?') + ' campos/frame).');
  await conn.disconnect(true);
  process.exit(3);
}

await conn.disconnect(true);
process.exit(0);
