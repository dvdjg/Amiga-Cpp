#!/usr/bin/env node
import fs from 'node:fs';

const [a, b] = process.argv.slice(2);
if (!a || !b) throw new Error('Uso: node ollama-compare-files.mjs <imagen-real> <imagen-esperada>');
const base = 'http://127.0.0.1:11434';
const model = process.env.OLLAMA_VL_MODEL || 'qwen3-vl:8b-instruct-q8_0';
const prompt = `Compara estas dos imágenes recortadas de un mapa de tiles de Amiga.
La primera es lo que se ve realmente en WinUAE y la segunda es lo esperado.
Los bordes exteriores son recortes arbitrarios y deben ignorarse.
Analiza solo el área del mapa, no el marco ni el HUD.
Comprueba específicamente si las dos primeras filas de tiles de la imagen real
aparecen arriba cuando deberían aparecer abajo, y si hay una discontinuidad o
costura horizontal alrededor de la tercera fila. Describe qué elementos aparecen
en las filas 0, 1, 2 y 3 de cada imagen y termina con un veredicto: IGUALES,
DESPLAZAMIENTO VERTICAL, o CONTENIDO DISTINTO. Sé conciso y no especules sobre
la causa técnica.`;
const response = await fetch(`${base}/api/chat`, {
  method: 'POST', headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ model, stream: false, messages: [{ role: 'user', content: prompt,
    images: [fs.readFileSync(a).toString('base64'), fs.readFileSync(b).toString('base64')] }] }),
  signal: AbortSignal.timeout(180000),
});
if (!response.ok) throw new Error(`Ollama HTTP ${response.status}`);
const json = await response.json();
console.log(json.message?.content ?? JSON.stringify(json));
