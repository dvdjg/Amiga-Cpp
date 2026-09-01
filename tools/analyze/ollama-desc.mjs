#!/usr/bin/env node
// Analiza frames de una demo con Ollama local (qwen3-vl), con VERIFICACIÓN de
// salud previa: si 127.0.0.1:11434 no responde, intenta arrancar `ollama serve`
// y reintenta antes de abortar. Uso:
//   node tools/analyze/ollama-desc.mjs <dir> <idx0,idx1,...> ["prompt"]
(async () => {
  const fs = await import('node:fs');
  const path = await import('node:path');
  const { spawn } = await import('node:child_process');

  // ---- 1) Verificación de salud / arranque de Ollama ----
  const BASE = 'http://127.0.0.1:11434';
  async function health() {
    try { const r = await fetch(`${BASE}/api/version`, { signal: AbortSignal.timeout(2000) }); return r.ok ? (await r.json()) : null; }
    catch { return null; }
  }
  async function startOllama() {
    const candidates = [
      'C:\\Users\\dvdjg\\AppData\\Local\\Programs\\Ollama\\ollama.exe',
      process.env.USERPROFILE + '\\AppData\\Local\\Programs\\Ollama\\ollama.exe',
      'C:\\Program Files\\Ollama\\ollama.exe',
    ];
    const exe = candidates.find((c) => fs.existsSync(c));
    if (!exe) return false;
    const child = spawn(exe, ['serve'], { stdio: 'ignore', detached: true });
    child.unref();
    // Espera hasta ~12 s (transparencia: qué está pasando en vez de silencio).
    for (let i = 0; i < 24; i++) { await new Promise((r) => setTimeout(r, 500)); const v = await health(); if (v) return v; }
    return false;
  }

  let ver = await health();
  if (!ver) {
    console.log('[ollama] no responde en 127.0.0.1:11434; arrancando `ollama serve`...');
    ver = await startOllama();
    if (!ver) { console.error('[ollama] NO disponible tras intentar arrancarlo.'); process.exit(2); }
    console.log(`[ollama] arrancado OK (${ver.version})`);
  } else {
    console.log(`[ollama] salud OK (${ver.version})`);
  }

  // ---- 2) Modelo ----
  const model = process.env.OLLAMA_VL_MODEL || 'qwen3-vl:8b-instruct-q8_0';
  try {
    const ml = await (await fetch(`${BASE}/api/tags`)).json();
    const have = (ml.models || []).some((m) => (m.name || '').split(':')[0] === model.split(':')[0]);
    if (!have) console.warn(`[ollama] aviso: no veo ${model.split(':')[0]}:* (listado: ${(ml.models || []).map((m) => m.name).join(', ') || 'ninguno'})`);
  } catch { /* no fatal */ }

  // ---- 3) Análisis de frames ----
  const dir = process.argv[2];
  const idxs = (process.argv[3] || '0').split(',').map(Number);
  const prompt = process.argv[4] || 'Describe brevemente lo que se ve.';
  const files = fs.readdirSync(dir).filter((f) => /^frame_\d{3}\.png$/.test(f)).sort();
  const images = idxs.map((i) => fs.readFileSync(path.join(dir, files[i])).toString('base64'));
  const res = await fetch(`${BASE}/api/chat`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ model, messages: [{ role: 'user', content: prompt, images }], stream: false }),
    signal: AbortSignal.timeout(180000),
  });
  const j = await res.json();
  console.log(j.message?.content ?? JSON.stringify(j).slice(0, 600));
})();