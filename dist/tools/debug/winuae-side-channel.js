#!/usr/bin/env node
/**
 * Cliente de linea de comandos del canal lateral de depuracion WinUAE-DBG.
 *
 * El canal lateral es un socket TCP local (por defecto 127.0.0.1:2346) en el que
 * se puede observar y controlar el proceso Amiga en ejecucion sin competir por el
 * socket GDB principal: lectura de memoria (`mem`), escritura reversible (`poke`),
 * registros (`regs`), capturas (`screenshot`), profiling (`profile`) y pausa/resume.
 *
 * Uso: node dist/tools/debug/winuae-side-channel.js <comando> [args...] [--port 2346]
 * Cada respuesta es una unica linea JSON. El protocolo se especifica en
 * docs/emulation/WINUAE_SIDE_CHANNEL_DEBUG.md.
 */
import * as net from 'net';
/**
 * Devuelve el valor de un argumento CLI del tipo `--clave valor`.
 * @param name Nombre del argumento (con doble guion).
 * @param fallback Valor por defecto si no se pasa el argumento.
 * @returns El valor del argumento o `fallback`.
 */
function argValue(name, fallback) {
    const index = process.argv.indexOf(name);
    if (index >= 0 && index + 1 < process.argv.length) {
        return process.argv[index + 1];
    }
    return fallback;
}
/**
 * Imprime la sintaxis de uso en stderr y termina el proceso con codigo 2.
 * @returns Nunca retorna (termina el proceso).
 */
function printUsageAndExit() {
    console.error('Uso: node dist/tools/debug/winuae-side-channel.js <comando> [args...] [--port 2346]');
    console.error('Comandos: hello | state | regs | mem <addr> <len> | runstatus <addr>');
    console.error('          lock status | lock acquire <owner> [observe|assist|takeover] | lock release [owner]');
    console.error('          screenshot <path> | input ... | profile ... | profile-status | action status <id>');
    console.error('          poke <addr> <hex-bytes> [label] | rollback <write-id> | audit writes | audit write <id>');
    console.error('          pause | resume');
    process.exit(2);
}
/**
 * Construye la orden del canal lateral uniendo los argumentos restantes.
 * Descarta el argumento `--port` y su valor, que son propios del cliente.
 * @returns Lista de palabras que formaran la orden.
 */
function buildCommandParts() {
    const parts = [];
    for (let i = 2; i < process.argv.length; i++) {
        if (process.argv[i] === '--port') {
            i++;
            continue;
        }
        parts.push(process.argv[i]);
    }
    return parts;
}
/**
 * Punto de entrada del CLI.
 *
 * Conecta al canal lateral, consume la linea de saludo del servidor, envia la
 * orden solicitada e imprime la respuesta JSON. Si el servidor no responde en
 * 2 segundos, termina con error.
 */
function main() {
    const commandParts = buildCommandParts();
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
                // El servidor envia una linea de saludo al conectar; solo despues
                // de consumirla mandamos la orden real para mantener la herramienta
                // predecible en scripts.
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
}
main();
