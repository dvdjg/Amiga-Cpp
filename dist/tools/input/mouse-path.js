#!/usr/bin/env node
import * as fs from 'fs';
import * as path from 'path';
import { pathToFileURL } from 'url';
import { repoRoot } from '../lib/paths.js';
const root = repoRoot(import.meta.url);
// El conector de WinUAE (repo hermano mcp-winuae-emu) vive junto al repositorio.
// Se importa dinamicamente desde la raiz para que la ruta valga tanto en el
// fuente como en dist/ (compilar anade un nivel de directorio).
const mcpRoot = path.join(path.dirname(root), 'mcp-winuae-emu', 'dist');
const [mcpWinuae, mcpMousePath] = await Promise.all([
    import(pathToFileURL(path.join(mcpRoot, 'winuae-connection.js')).href),
    import(pathToFileURL(path.join(mcpRoot, 'mouse-path.js')).href),
]);
const { WinUAEConnection } = mcpWinuae;
const { buildMousePathPoints } = mcpMousePath;
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
    // The monitor command receives Amiga display coordinates, not Windows
    // pointer coordinates.  We round and clamp them here so tests can use curves
    // freely without producing values outside the visible low-res A500 area.
    return buildMousePathPoints({
        from,
        to,
        control: control ?? undefined,
        control2: control2 ?? undefined,
        steps,
        maxX,
        maxY,
        ease: 'linear',
    });
}
function findExtensionRoot() {
    if (process.env.AMIGA_DEBUG_EXT && fs.existsSync(process.env.AMIGA_DEBUG_EXT)) {
        return path.resolve(process.env.AMIGA_DEBUG_EXT);
    }
    // Version-agnostic: pick the highest bartmanabyss.amiga-debug-* installed.
    const bases = [
        path.join(process.env.USERPROFILE || '', '.cursor/extensions'),
        path.join(process.env.USERPROFILE || '', '.vscode/extensions'),
    ];
    let best = '';
    let bestDir = null;
    for (const base of bases) {
        if (!fs.existsSync(base))
            continue;
        for (const entry of fs.readdirSync(base)) {
            const m = /^bartmanabyss\.amiga-debug-(.+)$/.exec(entry);
            if (!m)
                continue;
            const candidate = path.join(base, entry);
            if (!fs.existsSync(path.join(candidate, 'bin/win32/winuae-gdb.exe')))
                continue;
            if (m[1] > best) {
                best = m[1];
                bestDir = candidate;
            }
        }
    }
    if (bestDir)
        return bestDir;
    throw new Error('No se encontro la extension bartmanabyss.amiga-debug-*.');
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
async function sendMouseMove(protocol, dx, dy) {
    const reply = await protocol.sendMonitorCommand(`input mouse move ${dx} ${dy}`, 5000);
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
  --coordinate-mode mode  relative (default, delta desde el cursor del juego) o absolute (mousehack/tablet).
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
const coordinateMode = argValue('--coordinate-mode', 'relative');
if (coordinateMode !== 'relative' && coordinateMode !== 'absolute') {
    throw new Error('--coordinate-mode debe ser relative o absolute.');
}
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
    coordinateMode,
    button,
    clicked: hasArg('--click'),
    dragged: hasArg('--drag'),
    status: 'started',
};
try {
    await conn.connectExisting({ forceBreak: false, initializeStopped: false });
    const protocol = conn.getProtocol();
    if (hasArg('--drag') && coordinateMode === 'absolute') {
        await sendMouseAbs(protocol, points[0]);
    }
    if (hasArg('--drag')) {
        await sendMouseButton(protocol, button, 1);
    }
    let previous = points[0];
    for (const [index, point] of points.entries()) {
        if (coordinateMode === 'absolute') {
            await sendMouseAbs(protocol, point);
        }
        else {
            const dx = point.x - previous.x;
            const dy = point.y - previous.y;
            if (dx !== 0 || dy !== 0) {
                await sendMouseMove(protocol, dx, dy);
            }
        }
        previous = point;
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
}
finally {
    await conn.disconnect(false);
}
