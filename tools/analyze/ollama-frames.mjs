#!/usr/bin/env node
/**
 * Analiza la continuidad del scroll de una secuencia de frames usando Ollama local
 * (sin tokens de nube). Envia varios frames consecutivos a un modelo de vision
 * (gemma3:12b) y pregunta si el contenido se desplaza de forma continua o se
 * repite/salta.
 *
 * Uso:
 *   node tools/analyze/ollama-frames.mjs <out/run/<demo>/sequence> [--start N] [--count M] [--model gemma3:12b]
 *
 * Ejemplo:
 *   node tools/analyze/ollama-frames.mjs out/run/104_tile_scroll_ring_dualpf/sequence
 */
import * as fs from 'fs';
import * as path from 'path';

const rawHost = process.env.OLLAMA_HOST || '';
const BASE = rawHost.includes('://')
  ? rawHost
  : `http://${rawHost && !rawHost.startsWith('0.0.0.0') ? rawHost : '127.0.0.1:11434'}`;

function argValue(name, fallback) {
  const i = process.argv.indexOf(name);
  return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : fallback;
}

async function chat(modelName, messages) {
  const res = await fetch(`${BASE}/api/chat`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ model: modelName, messages, stream: false }),
  });
  if (!res.ok) throw new Error(`Ollama HTTP ${res.status}: ${await res.text()}`);
  const data = await res.json();
  return data.message?.content ?? '';
}

const seqDir = process.argv[2];
if (!seqDir || !fs.existsSync(seqDir)) {
  console.error('Uso: node tools/analyze/ollama-frames.mjs <secuencia> [--start N] [--count M] [--model X]');
  process.exit(2);
}

const start = parseInt(argValue('--start', '0'), 10);
const count = parseInt(argValue('--count', '5'), 10);
const model = argValue('--model', 'gemma3:12b');

const files = fs.readdirSync(seqDir)
  .filter((n) => n.endsWith('.png'))
  .sort()
  .slice(start, start + count);

if (files.length < 2) {
  console.error('Necesita al menos 2 frames para comparar.');
  process.exit(2);
}

const prompt = [
  'Estas son DOS capturas de una demo de scroll de tiles del Amiga (320x256) tomadas ~2 segundos aparte.',
  'Compara el CONTENIDO de la escena (los patrones/tiles, no el movimiento):',
  '1) ¿El mundo ha avanzado MUCHO (varios tiles/columnas cambiaron), POCO (solo unos pixels), o es la MISMA escena repitiendose?',
  '2) ¿Se ve contenido distinto a la izquierda/derecha/arriba/abajo?',
  '3) Responde concreto: MUCHO / POCO / IGUAL, y la direccion si la hay.',
].join('\n');

const messages = [
  {
    role: 'user',
    content: prompt,
    images: files.map((f) => fs.readFileSync(path.join(seqDir, f)).toString('base64')),
  },
];

console.log(`[ollama] analizando ${files.length} frames (${files[0]}..${files[files.length - 1]}) con ${model}...`);
try {
  const text = await chat(model, messages);
  console.log(text);
} catch (e) {
  console.error('[ollama] error:', e.message);
  process.exit(1);
}
