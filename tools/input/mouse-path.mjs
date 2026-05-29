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
    throw new Error(`${name} debe tener formato x,y. Ejemplo: --from 32,32`);
  }
  return { x: Number(match[1]), y: Number(match[2]) };
}

function clampInt(value, min, max) {
  return Math.max(min, Math.min(max, Math.round(value)));
}

function lerp(a, b, t) {
  return a + (b - a) * t;
}

function quadraticBezier(from, control, to, t) {
  const ab = { x: lerp(from.x, control.x, t), y: lerp(from.y, control.y, t) };
  const bc = { x: lerp(control.x, to.x, t), y: lerp(control.y, to.y, t) };
  return { x: lerp(ab.x, bc.x, t), y: lerp(ab.y, bc.y, t) };
}

function cubicBezier(from, c1, c2, to, t) {
  const ab = { x: lerp(from.x, c1.x, t), y: lerp(from.y, c1.y, t) };
  const bc = { x: lerp(c1.x, c2.x, t), y: lerp(c1.y, c2.y, t) };
  const cd = { x: lerp(c2.x, to.x, t), y: lerp(c2.y, to.y, t) };
  const abbc = { x: lerp(ab.x, bc.x, t), y: lerp(ab.y, bc.y, t) };
  const bccd = { x: lerp(bc.x, cd.x, t), y: lerp(bc.y, cd.y, t) };
  return { x: lerp(abbc.x, bccd.x, t), y: lerp(abbc.y, bccd.y, t) };
}

function buildPath() {
  const from = parsePoint(argValue('--from', '16,16'), '--from');
  const to = parsePoint(argValue('--to', '304,240'), '--to');
  const steps = Math.max(1, parseInt(argValue('--steps', '48'), 10));
  const maxX = parseInt(argValue('--max-x', '319'), 10);
  const maxY = parseInt(argValue('--max-y', '255'), 10);
  const controlText = argValue('--control');
  const control2Text = argValue('--control2');
  const control = controlText ? parsePoint(controlText, '--control') : null;
  const control2 = control2Text ? parsePoint(control2Text, '--control2') : null;
  const points = [];

  // The monitor command receives Amiga display coordinates, not Windows
  // pointer coordinates.  We round and clamp them here so tests can use curves
  // freely without producing values outside the visible low-res A500 area.
  for (let i = 0; i <= steps; i++) {
    const t = i / steps;
    let p;
    if (control && control2) {
      p = cubicBezier(from, control, control2, to, t);
    } else if (control) {
      p = quadraticBezier(from, control, to, t);
    } else {
      p = { x: lerp(from.x, to.x, t), y: lerp(from.y, to.y, t) };
    }
    points.push({
      x: clampInt(p.x, 0, maxX),
      y: clampInt(p.y, 0, maxY),
    });
  }

  return points;
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

function decodeMonitorReply(hexReply) {
  if (!hexReply || !/^[0-9a-fA-F]+$/.test(hexReply)) {
    return '';
  }
  return Buffer.from(hexReply, 'hex').toString('utf8').trim();
}

async function sendMouseAbs(protocol, point) {
  const reply = await protocol.sendMonitorCommand(`input mouse abs ${point.x} ${point.y}`, 5000);
  return decodeMonitorReply(reply);
}

async function sendMouseButton(protocol, button, state) {
  const reply = await protocol.sendMonitorCommand(`input mouse button ${button} ${state}`, 5000);
  return decodeMonitorReply(reply);
}

if (hasArg('--help') || hasArg('-h')) {
  console.log(`Uso:
  node tools/input/mouse-path.mjs [opciones]

Opciones principales:
  --from x,y              Punto inicial en coordenadas Amiga. Default: 16,16
  --to x,y                Punto final en coordenadas Amiga. Default: 304,240
  --control x,y           Punto de control para curva cuadratica.
  --control2 x,y          Segundo punto de control para curva cubica.
  --steps n               Numero de pasos de la trayectoria. Default: 48
  --duration-ms n         Duracion total de la trayectoria. Default: 800
  --button n              Boton Amiga/WinUAE: 0 izq, 1 der, 2 medio. Default: 0
  --click                 Pulsa y suelta al final de la trayectoria.
  --drag                  Mantiene el boton pulsado durante la trayectoria.
  --screenshot file.png   Captura pantalla tras aplicar la entrada.
  --port n                Puerto GDB de WinUAE. Default: 2345

Ejemplo:
  node tools/input/mouse-path.mjs --from 32,40 --to 280,170 --control 160,10 --click
`);
  process.exit(0);
}

const points = buildPath();
const durationMs = Math.max(0, parseInt(argValue('--duration-ms', '800'), 10));
const delayMs = points.length > 1 ? Math.floor(durationMs / (points.length - 1)) : 0;
const button = Math.max(0, parseInt(argValue('--button', '0'), 10));
const config = {
  winuaePath: path.join(findExtensionRoot(), 'bin/win32'),
  configFile: path.resolve(argValue('--config', path.join(root, 'config/mcp-amiga-c-debug.uae'))),
  gdbPort: parseInt(argValue('--port', process.env.WINUAE_GDB_PORT || '2345'), 10),
};

const conn = new WinUAEConnection(config);
const screenshotArg = argValue('--screenshot');
const screenshotPath = screenshotArg ? path.resolve(screenshotArg) : null;
const report = {
  points: points.length,
  first: points[0],
  last: points[points.length - 1],
  durationMs,
  button,
  clicked: hasArg('--click'),
  dragged: hasArg('--drag'),
  status: 'started',
};

try {
  await conn.connectExisting({ forceBreak: false, initializeStopped: false });
  const protocol = conn.getProtocol();

  if (hasArg('--drag')) {
    await sendMouseButton(protocol, button, 1);
  }

  for (const [index, point] of points.entries()) {
    await sendMouseAbs(protocol, point);
    if (delayMs > 0 && index + 1 < points.length) {
      await sleep(delayMs);
    }
  }

  if (hasArg('--drag')) {
    await sendMouseButton(protocol, button, 0);
  }

  if (hasArg('--click')) {
    await sendMouseButton(protocol, button, 1);
    await sleep(Math.max(20, parseInt(argValue('--click-ms', '80'), 10)));
    await sendMouseButton(protocol, button, 0);
  }

  if (screenshotPath) {
    fs.mkdirSync(path.dirname(screenshotPath), { recursive: true });
    const winScreenshotPath = screenshotPath.replace(/\//g, '\\');
    const reply = await protocol.sendMonitorCommand(`screenshot ${winScreenshotPath}`, 30000);
    report.screenshotReply = decodeMonitorReply(reply);
    report.screenshot = screenshotPath;
    report.screenshotExists = fs.existsSync(screenshotPath);
  }

  report.status = 'ok';
  console.log(JSON.stringify(report, null, 2));
} finally {
  await conn.disconnect(false);
}
