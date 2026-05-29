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

const waitMs = parseInt(argValue('--wait-ms', process.env.AMG_RUN_WAIT_MS || '12000'), 10);
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
