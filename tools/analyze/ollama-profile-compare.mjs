#!/usr/bin/env node
import fs from 'node:fs';

const [profileFile, indexes = '0,1,10,20,30,40,49'] = process.argv.slice(2);
if (!profileFile) throw new Error('Uso: node ollama-profile-compare.mjs <perfil.amigaprofile> [indices]');
const profile = JSON.parse(fs.readFileSync(profileFile, 'utf8'));
const frames = indexes.split(',').map(Number);
const images = frames.map((i) => String(profile.screenshots[i]).replace(/^data:image\/[^;]+;base64,/, ''));
const prompt = `Analiza una secuencia de ${frames.length} capturas de un scroll vertical de un mapa de tiles.
Los bordes y el HUD deben ignorarse. Las imágenes están en orden temporal: ${frames.join(', ')}.
Describe solo si existe una discontinuidad vertical: si la fila superior de una captura
contiene contenido que debería estar en la parte inferior, o si dos filas aparecen
intercambiadas. Compara especialmente los frames consecutivos alrededor del cambio de
posición. Indica el primer frame afectado y si el desplazamiento es de 16 o 32 píxeles.
Termina con VEREDICTO: CORRECTO, DESPLAZAMIENTO 16PX, DESPLAZAMIENTO 32PX o OTRO.
No atribuyas todavía la causa al Copper.`;
const r = await fetch('http://127.0.0.1:11434/api/chat', {
  method: 'POST', headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ model: process.env.OLLAMA_VL_MODEL || 'qwen3-vl:8b-instruct-q8_0', stream: false,
    messages: [{ role: 'user', content: prompt, images }] }),
  signal: AbortSignal.timeout(180000),
});
if (!r.ok) throw new Error(`Ollama HTTP ${r.status}`);
const j = await r.json();
console.log(j.message?.content ?? JSON.stringify(j));
