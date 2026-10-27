#!/usr/bin/env node
/**
 * Envía a Ollama SOLO un recorte de una o dos capturas, para que el modelo no
 * se distraiga con HUD, agua, árboles u otros elementos irrelevantes.
 *
 * Uso:
 *   node tools/analyze/ollama-crop.mjs captura.png --crop 300,120,220,180 \
 *     --prompt "Describe solo las flores."
 *
 *   node tools/analyze/ollama-crop.mjs a.png b.png --crop 300,120,220,180 \
 *     --prompt "Izquierda=A, derecha=B. ¿Cambian las flores?"
 *
 * Requiere Ollama local en 127.0.0.1:11434 y pngjs (dependencia del repo).
 */
import fs from 'node:fs';
import { PNG } from 'pngjs';

const args = process.argv.slice(2);
const flag = (name, fallback) => {
  const i = args.indexOf(name);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : fallback;
};
const images = args.filter((a) => !a.startsWith('--') && a !== flag('--crop', '') &&
  a !== flag('--prompt', '') && a !== flag('--model', '') && a !== flag('--scale', ''));
const cropArg = flag('--crop', '0,0,320,208');
const scale = Math.max(1, parseInt(flag('--scale', '4'), 10));
const prompt = flag('--prompt', 'Describe brevemente solo el contenido del recorte.');
const model = flag('--model', 'qwen3-vl:8b-instruct-q8_0');
const [cx, cy, cw, ch] = cropArg.split(',').map((v) => parseInt(v, 10));
if (images.length === 0 || [cx, cy, cw, ch].some((v) => !Number.isInteger(v) || v < 0) || cw < 1 || ch < 1) {
  console.error('Uso: node tools/analyze/ollama-crop.mjs <img> [img2] --crop x,y,w,h [--scale 4] [--prompt ...]');
  process.exit(2);
}

function crop(path) {
  const p = PNG.sync.read(fs.readFileSync(path));
  if (cx + cw > p.width || cy + ch > p.height) {
    throw new Error(`crop fuera de ${path}: ${p.width}x${p.height}`);
  }
  const out = new PNG({ width: cw, height: ch });
  for (let y = 0; y < ch; y++) {
    for (let x = 0; x < cw; x++) {
      const s = ((y + cy) * p.width + (x + cx)) * 4;
      const d = (y * cw + x) * 4;
      out.data[d] = p.data[s];
      out.data[d + 1] = p.data[s + 1];
      out.data[d + 2] = p.data[s + 2];
      out.data[d + 3] = p.data[s + 3];
    }
  }
  return out;
}

function upscale(p) {
  if (scale === 1) return p;
  const out = new PNG({ width: p.width * scale, height: p.height * scale });
  for (let y = 0; y < p.height; y++) {
    for (let x = 0; x < p.width; x++) {
      const s = (y * p.width + x) * 4;
      for (let sy = 0; sy < scale; sy++) {
        for (let sx = 0; sx < scale; sx++) {
          const d = ((y * scale + sy) * out.width + (x * scale + sx)) * 4;
          for (let k = 0; k < 4; k++) out.data[d + k] = p.data[s + k];
        }
      }
    }
  }
  return out;
}

let image;
if (images.length === 1) {
  image = upscale(crop(images[0]));
} else {
  const a = upscale(crop(images[0]));
  const b = upscale(crop(images[1]));
  image = new PNG({ width: a.width + b.width, height: Math.max(a.height, b.height) });
  for (let y = 0; y < image.height; y++) {
    for (let x = 0; x < image.width; x++) {
      const src = x < a.width ? a : b;
      const sx = x < a.width ? x : x - a.width;
      const si = (y * src.width + sx) * 4;
      const di = (y * image.width + x) * 4;
      if (y >= src.height) continue;
      for (let k = 0; k < 4; k++) image.data[di + k] = src.data[si + k];
    }
  }
}

const rawHost = (process.env.OLLAMA_HOST || '127.0.0.1:11434').replace(/^https?:\/\//, '');
const base = rawHost.startsWith('0.0.0.0') || rawHost === ''
  ? '127.0.0.1:11434' : rawHost;
const response = await fetch(`http://${base}/api/chat`, {
  method: 'POST',
  headers: { 'content-type': 'application/json' },
  body: JSON.stringify({
    model,
    stream: false,
    messages: [{
      role: 'user',
      content: prompt,
      images: [PNG.sync.write(image).toString('base64')],
    }],
  }),
});
if (!response.ok) {
  console.error(`[ollama-crop] HTTP ${response.status}: ${await response.text()}`);
  process.exit(1);
}
const json = await response.json();
console.log(json.message?.content ?? JSON.stringify(json).slice(0, 800));
