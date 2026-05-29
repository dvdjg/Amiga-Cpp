#!/usr/bin/env node
import * as net from 'net';

function argValue(name, fallback = undefined) {
  const index = process.argv.indexOf(name);
  if (index >= 0 && index + 1 < process.argv.length) {
    return process.argv[index + 1];
  }
  return fallback;
}

function printUsageAndExit() {
  console.error('Uso: node tools/debug/winuae-side-channel.mjs <comando> [args...] [--port 2346]');
  console.error('Comandos: hello | state | regs | mem <addr> <len> | runstatus <addr>');
  process.exit(2);
}

const commandParts = [];
for (let i = 2; i < process.argv.length; i++) {
  if (process.argv[i] === '--port') {
    i++;
    continue;
  }
  commandParts.push(process.argv[i]);
}

if (commandParts.length === 0) {
  printUsageAndExit();
}

const port = parseInt(argValue('--port', process.env.WINUAE_SIDE_CHANNEL_PORT || '2346'), 10);
const command = commandParts.join(' ');
const socket = net.createConnection({ host: '127.0.0.1', port });
socket.setEncoding('utf8');

let pending = '';
let consumedGreeting = false;
let done = false;
const timer = setTimeout(() => {
  if (!done) {
    socket.destroy();
    console.error(`Timeout esperando respuesta del canal lateral en 127.0.0.1:${port}`);
    process.exit(1);
  }
}, 2000);

socket.on('connect', () => {
  // El servidor envia una linea de saludo al conectar. En cuanto la consumimos,
  // mandamos la orden real para mantener la herramienta predecible en scripts.
});

socket.on('data', (chunk) => {
  pending += chunk;
  for (;;) {
    const eol = pending.indexOf('\n');
    if (eol < 0) {
      break;
    }
    const line = pending.slice(0, eol).trim();
    pending = pending.slice(eol + 1);
    if (!consumedGreeting) {
      consumedGreeting = true;
      socket.write(`${command}\n`);
      continue;
    }
    done = true;
    clearTimeout(timer);
    console.log(line);
    socket.end();
    return;
  }
});

socket.on('error', (err) => {
  clearTimeout(timer);
  console.error(`No se pudo conectar al canal lateral: ${err.message}`);
  process.exit(1);
});
