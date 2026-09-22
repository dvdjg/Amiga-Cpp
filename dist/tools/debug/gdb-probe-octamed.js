#!/usr/bin/env node
/**
 * gdb-probe-octamed.mjs (fuente TS) — Diagnostico del cuelgue de A1 (OctaMED) con GDB.
 *
 * Lanza WinUAE con la demo 274, pone breakpoints en simbolos del playroutine
 * (resolviendo la direccion RUNTIME, porque WinUAE-DBG relocaliza) y reporta paradas
 * con PC/registros. Sirve para ver donde se atasca, en vez de parchear a ciegas.
 *
 * Uso:
 *   node dist/tools/debug/gdb-probe-octamed.js \
 *       [--config <id>] [--bps _startmusic,_endmusic] [--hold-ms 8000]
 */
import * as fs from 'fs';
import * as path from 'path';
import { pathToFileURL } from 'url';
import { repoRoot } from '../lib/paths.js';
const root = repoRoot(import.meta.url);
const mcpWinuae = await import(pathToFileURL(path.join(path.dirname(root), 'mcp-winuae-emu', 'dist', 'winuae-connection.js')).href);
const { WinUAEConnection } = mcpWinuae;
function argValue(name, fallback = undefined) {
    const i = process.argv.indexOf(name);
    return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : fallback;
}
function sleep(ms) { return new Promise((r) => setTimeout(r, ms)); }
function parseHex(v) { return parseInt(v.replace(/^0x/i, ''), 16); }
function setConfigValue(t, k, v) {
    const re = new RegExp(`^${k.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}=.*$`, 'm');
    const line = `${k}=${v}`;
    return re.test(t) ? t.replace(re, line) : `${t.replace(/\s*$/, '')}\r\n${line}\r\n`;
}
function findExtensionRoot() {
    if (process.env.AMIGA_DEBUG_EXT && fs.existsSync(process.env.AMIGA_DEBUG_EXT)) {
        return path.resolve(process.env.AMIGA_DEBUG_EXT);
    }
    for (const base of [
        path.join(process.env.USERPROFILE || '', '.cursor/extensions'),
        path.join(process.env.USERPROFILE || '', '.vscode/extensions'),
    ]) {
        if (!fs.existsSync(base))
            continue;
        let best = '';
        for (const e of fs.readdirSync(base)) {
            if (!/^bartmanabyss\.amiga-debug-/.test(e))
                continue;
            if (!fs.existsSync(path.join(base, e, 'bin/win32/winuae-gdb.exe')))
                continue;
            if (!best || e > best)
                best = e;
        }
        if (best)
            return path.join(base, best);
    }
    throw new Error('No se encontro la extension bartmanabyss.amiga-debug-*.');
}
function findMapSymbol(mapPath, name) {
    if (!fs.existsSync(mapPath))
        return null;
    const re = new RegExp(`^\\s*0x([0-9a-fA-F]+)\\s+${name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\b`, 'm');
    const m = re.exec(fs.readFileSync(mapPath, 'utf8'));
    return m ? parseInt(m[1], 16) : null;
}
function findMapAllocSections(mapPath) {
    const wanted = new Set(['.text', '.rodata', '.eh_frame', '.data', '.bss']);
    const out = [];
    for (const line of fs.readFileSync(mapPath, 'utf8').split(/\r?\n/g)) {
        const m = line.match(/^(\.[A-Za-z0-9_.]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)/);
        if (!m || !wanted.has(m[1]))
            continue;
        const start = parseInt(m[2], 16);
        const size = parseInt(m[3], 16);
        if (size === 0)
            continue;
        out.push({ start, size, end: start + size });
    }
    return out;
}
function resolveRuntime(linked, mapSections, runtime) {
    if (!Array.isArray(runtime) || runtime.length === 0)
        return null;
    const cand = mapSections.filter((s) => linked >= s.start && linked < s.end);
    if (cand.length === 0)
        return parseHex(runtime[0]) + (linked - 0x400);
    const idx = mapSections.indexOf(cand[0]);
    if (runtime.length <= idx)
        return null;
    return parseHex(runtime[idx]) + (linked - cand[0].start);
}
const demoName = path.basename(argValue('--demo', 'demos/amiga/274_octamed_probe').replace(/\\/g, '/'));
const demoDir = path.join(root, 'out/demos', demoName);
const forced = argValue('--config', '');
let cfg = forced, exe = '', map = '';
if (forced) {
    exe = path.join(demoDir, forced, `${demoName}.${forced}.exe`);
    map = path.join(demoDir, forced, `${demoName}.${forced}.map`);
}
else {
    let best = null;
    for (const e of fs.readdirSync(demoDir)) {
        const dir = path.join(demoDir, e);
        if (!fs.statSync(dir).isDirectory())
            continue;
        const x = path.join(dir, `${demoName}.${e}.exe`);
        if (!fs.existsSync(x))
            continue;
        const mt = fs.statSync(x).mtimeMs;
        if (!best || mt > best.mt)
            best = { mt, cfg: e, exe: x, map: path.join(dir, `${demoName}.${e}.map`) };
    }
    if (!best)
        throw new Error(`Sin builds en ${demoDir}`);
    cfg = best.cfg;
    exe = best.exe;
    map = best.map;
}
const bpNames = argValue('--bps', '_startmusic,_endmusic').split(',').map((s) => s.trim()).filter(Boolean);
const holdMs = parseInt(argValue('--hold-ms', '8000'), 10);
console.log(`[gdb-probe] demo=${demoName} config=${cfg}\n[gdb-probe] exe=${exe}`);
const extensionRoot = findExtensionRoot();
const outputDir = path.join(root, 'out/run', demoName, cfg);
const stagedDir = path.join(outputDir, 'dh1');
fs.mkdirSync(stagedDir, { recursive: true });
fs.copyFileSync(exe, path.join(stagedDir, 'a.exe'));
const dh0 = path.join(extensionRoot, 'bin/dh0');
const startupPath = path.join(dh0, 's/startup-sequence');
fs.mkdirSync(path.dirname(startupPath), { recursive: true });
const prevStartup = fs.existsSync(startupPath) ? fs.readFileSync(startupPath, 'utf8') : null;
fs.writeFileSync(startupPath, 'cd dh1:\n:a.exe\n', 'utf8');
const cfgPath = path.join(outputDir, 'gdb-probe-uae.uae');
let configText = fs.readFileSync(path.join(root, 'config/mcp-amiga-c-debug.uae'), 'utf8');
configText = configText.replace(/^filesystem=rw,dh0:.*$/m, `filesystem=rw,dh0:${dh0.replace(/\//g, '\\')}`);
configText = setConfigValue(configText, 'filesystem2', `rw,dh1:dh1:${stagedDir.replace(/\//g, '\\')},-128`);
configText = setConfigValue(configText, 'debugging_trigger', ':a.exe');
configText = setConfigValue(configText, 'warp', 'false');
fs.writeFileSync(cfgPath, configText, 'utf8');
const conn = new WinUAEConnection({
    winuaePath: path.join(extensionRoot, 'bin/win32'),
    configFile: cfgPath,
    gdbPort: parseInt(process.env.WINUAE_GDB_PORT || '2345', 10),
});
const report = { demo: demoName, config: cfg, breakpoints: [], stops: [] };
try {
    await conn.connect({ forceBreak: false, initializeStopped: true });
    const proto = conn.getProtocol();
    const state = await proto.sendMonitorCommand ? null : null; // noop
    // Necesitamos las secciones runtime: usamos el canal lateral via protocolo si esta, si no
    // el propio gdbserver `qOffsets`. Aquí leemos `info files`-like via monitor no disponible;
    // usamos el canal lateral de run-demo no conectado, asi que pedimos secciones por GDB.
    const mapSections = findMapAllocSections(map);
    // Runtime sections: usar el symbol `g_eng_run_status` (seccion .bss) como ancla no basta;
    // pedir al gdbserver las direcciones de seccion. WinUAE-DBG expone `qOffsets`.
    let runtimeSections = [];
    try {
        const offsets = await proto.sendMonitorCommand('qOffsets', 5000);
        // qOffsets devuelve "Text=... Data=...” (direcciones runtime de las secciones).
        const hex = Buffer.from(offsets, 'hex').toString('utf8');
        const text = (/Text=([0-9a-fA-F]+)/.exec(hex) || [])[1];
        const data = (/Data=([0-9a-fA-F]+)/.exec(hex) || [])[1];
        if (text)
            runtimeSections = ['0x' + text];
        if (data)
            runtimeSections.push('0x' + data);
    }
    catch { /* qOffsets no soportado */ }
    for (const name of bpNames) {
        const linked = findMapSymbol(map, name);
        let addr = null;
        if (linked !== null && runtimeSections.length)
            addr = resolveRuntime(linked, mapSections, runtimeSections);
        report.breakpoints.push({ name, linked: linked !== null ? `0x${linked.toString(16)}` : null, runtime: addr !== null ? `0x${addr.toString(16)}` : null });
        if (addr !== null && addr > 0) {
            try {
                await proto.setBreakpoint('*0x' + addr.toString(16));
            }
            catch (e) {
                report.breakpoints[report.breakpoints.length - 1].error = String(e);
            }
        }
    }
    await proto.continue();
    const start = Date.now();
    while (Date.now() - start < holdMs) {
        let stop;
        try {
            stop = await proto.waitForStop(1500);
        }
        catch {
            continue;
        }
        if (/^W|^X/.test(stop)) {
            report.stops.push({ kind: 'exit', reply: stop });
            break;
        }
        const regs = await proto.readRegisters().catch(() => null);
        report.stops.push({ pc: regs ? '0x' + (regs.PC >>> 0).toString(16) : null, regs: regs ? { D0: regs.D0, A0: regs.A0, A1: regs.A1, A6: regs.A6 } : null });
        await proto.continue();
        if (report.stops.length > 40)
            break;
    }
    console.log(JSON.stringify(report, null, 2));
}
finally {
    try {
        await conn.disconnect(true);
    }
    catch { /* no ocultar */ }
    if (prevStartup !== null)
        fs.writeFileSync(startupPath, prevStartup, 'utf8');
}
