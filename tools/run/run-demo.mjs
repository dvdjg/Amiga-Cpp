#!/usr/bin/env node
import { WinUAEConnection } from '../../../mcp-winuae-emu/dist/winuae-connection.js';
import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';

const root = path.resolve(path.join(path.dirname(fileURLToPath(import.meta.url)), '../..'));

function argValue(name, fallback = undefined) {
  const index = process.argv.indexOf(name);
  if (index >= 0 && index + 1 < process.argv.length) {
    return process.argv[index + 1];
  }
  return fallback;
}

function hasArg(name) {
  return process.argv.includes(name);
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function parsePoint(text, name) {
  const match = String(text ?? '').match(/^\s*(-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?)\s*$/);
  if (!match) {
    throw new Error(`${name} debe tener formato x,y. Ejemplo: ${name} 32,32`);
  }
  return { x: Number(match[1]), y: Number(match[2]) };
}

function clampInt(value, min, max) {
  return Math.max(min, Math.min(max, Math.round(value)));
}

function lerp(a, b, t) {
  return a + (b - a) * t;
}

function curvePoint(from, control, to, t) {
  if (!control) {
    return { x: lerp(from.x, to.x, t), y: lerp(from.y, to.y, t) };
  }
  const ab = { x: lerp(from.x, control.x, t), y: lerp(from.y, control.y, t) };
  const bc = { x: lerp(control.x, to.x, t), y: lerp(control.y, to.y, t) };
  return { x: lerp(ab.x, bc.x, t), y: lerp(ab.y, bc.y, t) };
}

function buildMousePathFromArgs() {
  const fromText = argValue('--mouse-from');
  const toText = argValue('--mouse-to');
  if (!fromText && !toText) {
    return [];
  }
  if (!fromText || !toText) {
    throw new Error('Para automatizar raton hacen falta --mouse-from y --mouse-to.');
  }

  const from = parsePoint(fromText, '--mouse-from');
  const to = parsePoint(toText, '--mouse-to');
  const control = argValue('--mouse-control') ? parsePoint(argValue('--mouse-control'), '--mouse-control') : null;
  const steps = Math.max(1, parseInt(argValue('--mouse-steps', '48'), 10));
  const maxX = parseInt(argValue('--mouse-max-x', '319'), 10);
  const maxY = parseInt(argValue('--mouse-max-y', '255'), 10);
  const points = [];

  // These are Amiga display coordinates consumed by WinUAE monitor commands.
  // The Windows cursor is never moved: the emulated mouse receives synthetic
  // absolute positions through the debugger channel while the host pointer is
  // free to remain wherever the user left it.
  for (let i = 0; i <= steps; i++) {
    const p = curvePoint(from, control, to, i / steps);
    points.push({
      x: clampInt(p.x, 0, maxX),
      y: clampInt(p.y, 0, maxY),
    });
  }

  return points;
}

function escapeRegExp(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function setConfigValue(configText, key, value) {
  const line = `${key}=${value}`;
  const re = new RegExp(`^${escapeRegExp(key)}=.*$`, 'm');
  if (re.test(configText)) {
    return configText.replace(re, line);
  }
  return `${configText.replace(/\s*$/, '')}\r\n${line}\r\n`;
}

function findExtensionRoot() {
  if (process.env.AMIGA_DEBUG_EXT && fs.existsSync(process.env.AMIGA_DEBUG_EXT)) {
    return path.resolve(process.env.AMIGA_DEBUG_EXT);
  }

  const candidates = [
    path.join(process.env.USERPROFILE || '', '.cursor/extensions/bartmanabyss.amiga-debug-1.8.2'),
    path.join(process.env.USERPROFILE || '', '.vscode/extensions/bartmanabyss.amiga-debug-1.8.2'),
  ];

  for (const candidate of candidates) {
    if (fs.existsSync(path.join(candidate, 'bin/win32/winuae-gdb.exe'))) {
      return candidate;
    }
  }

  throw new Error('No se encontro la extension bartmanabyss.amiga-debug-1.8.2.');
}

function patchConfig(configText, extensionRoot, stagedOutDir) {
  const dh0 = path.join(extensionRoot, 'bin/dh0');
  const normalizedDh0 = dh0.replace(/\//g, '\\');
  const normalizedOut = stagedOutDir.replace(/\//g, '\\');

  let out = configText;
  out = out.replace(/^filesystem=rw,dh0:.*$/m, `filesystem=rw,dh0:${normalizedDh0}`);

  if (/^filesystem2=rw,dh1:.*$/m.test(out)) {
    out = out.replace(/^filesystem2=rw,dh1:.*$/m, `filesystem2=rw,dh1:dh1:${normalizedOut},-128`);
  } else {
    out += `\r\nfilesystem2=rw,dh1:dh1:${normalizedOut},-128\r\n`;
  }

  if (/^debugging_trigger=.*$/m.test(out)) {
    out = out.replace(/^debugging_trigger=.*$/m, 'debugging_trigger=:a.exe');
  } else {
    out += '\r\ndebugging_trigger=:a.exe\r\n';
  }

  // Automated tests must never trap the host pointer.  WinUAE-DBG has a
  // separate target option (`win32.absolute_mouse`) that disables the classic
  // relative mouse capture/warping path, while the Amiga-side tablet/mousehack
  // option (`absolute_mouse=mousehack`) remains disabled because that path is
  // still documented as problematic in WinUAE-DBG/doc/MOUSE-ABSOLUTE-TODO.md.
  out = setConfigValue(out, 'win32.start_not_captured', 'yes');
  out = setConfigValue(out, 'win32.active_capture_automatically', 'no');
  out = setConfigValue(out, 'win32.absolute_mouse', 'yes');
  out = setConfigValue(out, 'absolute_mouse', 'none');

  return out;
}

const demoArg = process.argv[2];
if (!demoArg || demoArg.startsWith('--')) {
  console.error('Uso: node tools/run/run-demo.mjs demos/000_toolchain_cpp23 [--wait-ms 12000] [--screenshot file.png]');
  process.exit(2);
}

const demoPath = path.resolve(root, demoArg);
const demoName = path.basename(demoPath);
const builtExe = path.join(root, 'out/demos', demoName, `${demoName}.exe`);
if (!fs.existsSync(builtExe)) {
  throw new Error(`No existe ${builtExe}. Compila la demo antes de ejecutarla.`);
}

const waitMs = parseInt(argValue('--wait-ms', process.env.AMG_RUN_WAIT_MS || '18000'), 10);
const mousePath = buildMousePathFromArgs();
const mouseDelayMs = Math.max(0, parseInt(argValue('--mouse-duration-ms', '800'), 10)) / Math.max(1, mousePath.length - 1);
const mouseButton = Math.max(0, parseInt(argValue('--mouse-button', '0'), 10));
const stopEmulator = !hasArg('--keep-running');
const outputDir = path.join(root, 'out/run', demoName);
const stagedDir = path.join(outputDir, 'dh1');
fs.mkdirSync(stagedDir, { recursive: true });
fs.copyFileSync(builtExe, path.join(stagedDir, 'a.exe'));

const screenshotPath = path.resolve(argValue('--screenshot', path.join(outputDir, 'screenshot.png')));
fs.mkdirSync(path.dirname(screenshotPath), { recursive: true });

const extensionRoot = findExtensionRoot();
const winuaePath = path.join(extensionRoot, 'bin/win32');
const dh0 = path.join(extensionRoot, 'bin/dh0');
const startupPath = path.join(dh0, 's/startup-sequence');
fs.mkdirSync(path.dirname(startupPath), { recursive: true });

const previousStartup = fs.existsSync(startupPath) ? fs.readFileSync(startupPath, 'utf8') : null;
fs.writeFileSync(startupPath, 'cd dh1:\n:a.exe\n', 'utf8');

const baseConfigPath = path.join(root, 'config/mcp-amiga-c-debug.uae');
const runnerConfigPath = path.join(outputDir, 'runner.uae');
const configText = fs.readFileSync(baseConfigPath, 'utf8');
fs.writeFileSync(runnerConfigPath, patchConfig(configText, extensionRoot, stagedDir), 'utf8');

const config = {
  winuaePath,
  configFile: runnerConfigPath,
  gdbPort: parseInt(process.env.WINUAE_GDB_PORT || '2345', 10),
};

const conn = new WinUAEConnection(config);
const report = {
  demo: demoName,
  executable: builtExe,
  stagedExecutable: path.join(stagedDir, 'a.exe'),
  screenshot: screenshotPath,
  waitMs,
  status: 'started',
};

try {
  console.log(`[run-demo] launching ${demoName}`);
  await conn.connect({ forceBreak: false, initializeStopped: false });
  const protocol = conn.getProtocol();
  await protocol.continue();
  console.log(`[run-demo] connected; waiting ${waitMs} ms`);
  await sleep(waitMs);

  if (mousePath.length > 0) {
    console.log(`[run-demo] injecting mouse path with ${mousePath.length} points`);
    if (hasArg('--mouse-drag')) {
      await protocol.sendMonitorCommand(`input mouse button ${mouseButton} 1`, 5000);
    }
    for (const [index, point] of mousePath.entries()) {
      await protocol.sendMonitorCommand(`input mouse abs ${point.x} ${point.y}`, 5000);
      if (mouseDelayMs > 0 && index + 1 < mousePath.length) {
        await sleep(mouseDelayMs);
      }
    }
    if (hasArg('--mouse-drag')) {
      await protocol.sendMonitorCommand(`input mouse button ${mouseButton} 0`, 5000);
    }
    if (hasArg('--mouse-click')) {
      await protocol.sendMonitorCommand(`input mouse button ${mouseButton} 1`, 5000);
      await sleep(Math.max(20, parseInt(argValue('--mouse-click-ms', '80'), 10)));
      await protocol.sendMonitorCommand(`input mouse button ${mouseButton} 0`, 5000);
    }
    report.mouse = {
      points: mousePath.length,
      first: mousePath[0],
      last: mousePath[mousePath.length - 1],
      button: mouseButton,
      clicked: hasArg('--mouse-click'),
      dragged: hasArg('--mouse-drag'),
    };
  }

  const winScreenshotPath = screenshotPath.replace(/\//g, '\\');
  const screenshotReply = await protocol.sendMonitorCommand(`screenshot ${winScreenshotPath}`, 30000);
  report.screenshotReply = screenshotReply;
  report.status = fs.existsSync(screenshotPath) ? 'ok' : 'missing_screenshot';

  fs.writeFileSync(path.join(outputDir, 'run-report.json'), JSON.stringify(report, null, 2), 'utf8');
  console.log(`[run-demo] screenshot ${screenshotPath}`);
  console.log(`[run-demo] ${report.status}`);
} finally {
  try {
    await conn.disconnect(stopEmulator);
    if (!stopEmulator && conn.process && typeof conn.process.unref === 'function') {
      conn.process.unref();
    }
  } catch {
    // Best effort cleanup: a failed disconnect should not hide the run result.
  }

  if (previousStartup !== null) {
    fs.writeFileSync(startupPath, previousStartup, 'utf8');
  }
}

if (report.status !== 'ok') {
  process.exit(1);
}
