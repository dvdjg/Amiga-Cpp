// Harness DAP para el adaptador Amiga. Conduce una sesion de depuracion
// completa (launch + setBreakpoints + configurationDone) y reporta paradas y
// stack trace. Sirve para verificar el flujo sin abrir VS Code.
//
// Uso:
//   node tools/dap-test/dap-client.js [puerto] [programaSinExt] [fuente] [linea]
//   Ejemplo:
//   node tools/dap-test/dap-client.js 4711 \
//     C:/.../Amiga-Cpp/out/debug-current/current \
//     C:/.../Amiga-Cpp/demos/050_blitter_bobs/src/main.cpp 296
//
// Requisitos: ver tools/dap-test/README.md (stub de 'vscode' + debugAdapter
// standalone).
const { spawn } = require('child_process');
const net = require('net');
const path = require('path');

const PORT = parseInt(process.argv[2] || '4711', 10);
const PROGRAM = process.argv[3] || 'C:/Users/dvdjg/Documents/programa/AI/Amiga/Amiga-Cpp/out/debug-current/current';
const SOURCE = process.argv[4] || 'C:/Users/dvdjg/Documents/programa/AI/Amiga/Amiga-Cpp/demos/050_blitter_bobs/src/main.cpp';
const LINE = parseInt(process.argv[5] || '296', 10);
const FORK = process.env.AMIGA_FORK || 'C:/Users/dvdjg/Documents/programa/AI/Amiga/vscode-amiga-debug';

const adapter = spawn('node', [path.join(FORK, 'dist/debugAdapter.js'), `--server=${PORT}`], {
  stdio: ['ignore', 'pipe', 'pipe']
});
adapter.stderr.on('data', (d) => console.error('[adapter]', d.toString().trim()));

let seq = 0;
const pending = new Map();
let sock;
let buf = Buffer.alloc(0);

function connectWithRetry(attempt) {
  sock = net.connect(PORT, '127.0.0.1', () => {
    console.log('connected');
    sock.on('data', onData);
  });
  sock.on('error', (err) => {
    if (attempt < 60) setTimeout(() => connectWithRetry(attempt + 1), 250);
    else { console.error('connect failed:', err.message); process.exit(1); }
  });
}

function write(body) {
  const payload = Buffer.from(body, 'utf8');
  const header = Buffer.from(`Content-Length: ${payload.length}\r\n\r\n`, 'ascii');
  sock.write(Buffer.concat([header, payload]));
}

function sendRequest(command, args) {
  return new Promise((resolve, reject) => {
    const id = ++seq;
    pending.set(id, { resolve, reject });
    write(JSON.stringify({ seq: id, type: 'request', command, arguments: args }));
  });
}

function onData(chunk) {
  buf = Buffer.concat([buf, chunk]);
  for (;;) {
    const idx = buf.indexOf('\r\n\r\n');
    if (idx < 0) return;
    const m = /Content-Length: (\d+)/i.exec(buf.slice(0, idx).toString('ascii'));
    if (!m) { buf = buf.slice(idx + 4); continue; }
    const len = parseInt(m[1], 10);
    if (buf.length < idx + 4 + len) return;
    const msg = buf.slice(idx + 4, idx + 4 + len).toString('utf8');
    buf = buf.slice(idx + 4 + len);
    let obj;
    try { obj = JSON.parse(msg); } catch (e) { continue; }
    if (obj.type === 'response') {
      const p = pending.get(obj.request_seq);
      if (p) { pending.delete(obj.request_seq); obj.success ? p.resolve(obj) : p.reject(new Error(JSON.stringify(obj.body))); }
    } else if (obj.type === 'event') {
      if (obj.event === 'stopped') {
        console.log(`\n=== stopped reason=${obj.body.reason} thread=${obj.body.threadId}`);
        handleStopped(obj.body.threadId).catch(console.error);
      } else if (obj.event === 'output') {
        const out = (obj.body.output || '').replace(/\n$/, '');
        if (out && out.includes('received:')) console.log('[qOffsets]', out.trim());
      } else if (obj.event === 'terminated') {
        console.log('=== terminated');
        setTimeout(() => process.exit(0), 300);
      }
    }
  }
}

async function handleStopped(threadId) {
  try {
    await sendRequest('threads', {});
    const st = await sendRequest('stackTrace', { threadId, startFrame: 0, levels: 6 });
    for (const f of (st.body.stackFrames || [])) {
      console.log('  ', `${f.name} @ ${f.source ? (f.source.path || f.source.name) : '?'} : ${f.line}`);
    }
  } catch (e) {
    console.error('handleStopped:', e.message);
  }
}

(async () => {
  connectWithRetry(0);
  await new Promise((r) => setTimeout(r, 800));
  await sendRequest('initialize', { adapterID: 'amiga', linesStartAt1: true, columnsStartAt1: true, pathFormat: 'path' })
    .then(() => console.log('initialize: ok')).catch((e) => console.log('initialize ERR', e.message));
  await sendRequest('launch', {
    config: 'A500', program: PROGRAM, kickstart: 'C:/Amiga/KICK13.rom', stack: '65536',
    breakpointRelocation: true, emuargs: ['-norawinput_mouse'], internalConsoleOptions: 'openOnSessionStart'
  }).then(() => console.log('launch: ok')).catch((e) => console.log('launch ERROR', e.message));
  await sendRequest('setBreakpoints', { source: { path: SOURCE }, breakpoints: [{ line: LINE }], sourceModified: false })
    .then((r) => console.log('setBreakpoints:', JSON.stringify(r.body))).catch((e) => console.log('setBreakpoints ERROR', e.message));
  await sendRequest('configurationDone', {}).then(() => console.log('configurationDone: ok')).catch(() => {});
  console.log('esperando paradas...');
  setTimeout(() => { console.log('TIMEOUT (sin parada)'); adapter.kill(); process.exit(2); }, 120000);
})();
