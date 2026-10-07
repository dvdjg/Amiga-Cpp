#!/usr/bin/env node
// Pasada de visión de una demo: copia los frames con NOMBRE CANÓNICO a
// `<demoDir>/vision/<demoId>_fNNNN.png` y escribe/agrega el informe crudo
// `<demoDir>/<demoId>_report.md`. Convención completa en
// docs/guides/methodology/DEMO_VISUAL_DEBUG.md §6.5.
//
// Uso:
//   node tools/analyze/vision-run.mjs <demoId> <dirSeq> <idx…>
//        --prompt "…" [--prompt "…"] [--model m] [--conclusion "…"]
//        [--demo-dir ruta] [--per-frame]
//
// - `idx` = número de la secuencia (frame_<idx>_f<frame>.png).
// - Modo por defecto: SECUENCIA (todas las imágenes en UNA llamada, con leyenda
//   de orden temporal) — preguntas de movimiento/continuidad.
// - `--per-frame`: una imagen por llamada (detalle de una captura concreta).
(async () => {
  const fs = await import('node:fs');
  const path = await import('node:path');
  const { spawn } = await import('node:child_process');

  const argv = process.argv.slice(2);
  const positional = [];
  const prompts = [];
  let model = 'qwen3-vl:8b-instruct-q8_0';
  let conclusion = '';
  let demoDirArg = '';
  let perFrame = false;
  for (let i = 0; i < argv.length; ++i) {
    const a = argv[i];
    if (a === '--prompt') prompts.push(argv[++i]);
    else if (a === '--model') model = argv[++i];
    else if (a === '--conclusion') conclusion = argv[++i];
    else if (a === '--demo-dir') demoDirArg = argv[++i];
    else if (a === '--per-frame') perFrame = true;
    else positional.push(a);
  }
  const [demoId, seqDir, ...idxRaw] = positional;
  const idxs = idxRaw.map((v) => parseInt(v, 10)).filter((v) => Number.isFinite(v));
  if (!demoId || !seqDir || idxs.length === 0 || prompts.length === 0) {
    console.error('uso: vision-run.mjs <demoId> <dirSeq> <idx…> --prompt "…" [--demo-dir ruta] [--per-frame]');
    process.exit(2);
  }

  // ---- Carpeta de la demo: --demo-dir o búsqueda por nombre bajo demos/ ----
  let demoDir = demoDirArg;
  if (!demoDir) {
    const hit = fs.readdirSync('demos', { recursive: true, withFileTypes: true })
      .find((e) => e.isDirectory() && e.name === demoId);
    if (!hit) { console.error(`[vision] no encuentro demos/**/${demoId}; pasa --demo-dir`); process.exit(3); }
    demoDir = path.join(hit.parentPath ?? hit.path, hit.name);
  }
  console.log(`[vision] demoDir = ${demoDir}`);

  // ---- Ollama: salud / arranque (mismo patrón que ollama-desc.mjs) ----
  const BASE = 'http://127.0.0.1:11434';
  async function health() {
    try { const r = await fetch(`${BASE}/api/version`, { signal: AbortSignal.timeout(2000) }); return r.ok ? (await r.json()) : null; }
    catch { return null; }
  }
  let ver = await health();
  if (!ver) {
    const exe = ['C:\\Users\\dvdjg\\AppData\\Local\\Programs\\Ollama\\ollama.exe',
      process.env.USERPROFILE + '\\AppData\\Local\\Programs\\Ollama\\ollama.exe',
      'C:\\Program Files\\Ollama\\ollama.exe'].find((c) => fs.existsSync(c));
    if (exe) {
      console.log('[ollama] no responde; arrancando `ollama serve`…');
      spawn(exe, ['serve'], { stdio: 'ignore', detached: true }).unref();
      for (let i = 0; i < 24 && !ver; i++) { await new Promise((r) => setTimeout(r, 500)); ver = await health(); }
    }
  }
  if (!ver) { console.error('[ollama] NO disponible.'); process.exit(2); }
  console.log(`[ollama] OK (${ver.version})`);

  // ---- Frames: localizar, copiar con nombre canónico ----
  const files = fs.readdirSync(seqDir).filter((f) => /^frame_\d+_f\d+\.png$/.test(f));
  const visionDir = path.join(demoDir, 'vision');
  fs.mkdirSync(visionDir, { recursive: true });
  const picked = [];
  for (const idx of idxs) {
    const m = files.map((f) => /^frame_(\d+)_f(\d+)\.png$/.exec(f)).find((x) => x && parseInt(x[1], 10) === idx);
    if (!m) { console.error(`[vision] no hay frame de secuencia idx=${idx} en ${seqDir}`); process.exit(3); }
    const frame = m[2].padStart(4, '0');
    const dst = path.join(visionDir, `${demoId}_f${frame}.png`);
    fs.copyFileSync(path.join(seqDir, m[0]), dst);
    picked.push({ idx, frame, dst });
  }
  console.log(`[vision] ${picked.length} capturas copiadas a ${visionDir}`);

  const legend = picked.map((p, i) => `imagen ${i + 1} = frame f${p.frame}`).join(', ');

  async function ask(images, prompt) {
    const body = { model, prompt, images, stream: false };
    const r = await fetch(`${BASE}/api/generate`, {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body),
      signal: AbortSignal.timeout(300000),
    });
    const j = await r.json();
    return (j.response ?? '').trim();
  }

  // ---- Informe ----
  const reportPath = path.join(demoDir, `${demoId}_report.md`);
  let out = '';
  const exists = fs.existsSync(reportPath);
  if (!exists) {
    out += `# Informe de ejecución — ${demoId}\n\n`;
    out += `Crudo de las pasadas de visión/medida. Convención: docs/guides/methodology/DEMO_VISUAL_DEBUG.md §6.5.\n`;
    out += `El resumen canónico commiteado vive en el VALIDATION.md de la demo.\n`;
  }
  out += `\n## Pasada ${new Date().toISOString()}\n\n`;
  out += `- Secuencia: \`${seqDir}\`\n- Frames: ${picked.map((p) => `f${p.frame}`).join(', ')} (capturas: \`vision/${demoId}_f*.png\`)\n`;
  out += `- Modelo: ${model} — modo: ${perFrame ? 'per-frame' : 'secuencia'}\n`;

  for (const prompt of prompts) {
    if (perFrame) {
      for (const p of picked) {
        const b64 = fs.readFileSync(p.dst).toString('base64');
        const resp = await ask([b64], prompt);
        out += `\n### f${p.frame} — prompt\n\n\`\`\`text\n${prompt}\n\`\`\`\n\n### f${p.frame} — respuesta cruda\n\n${resp}\n`;
        console.log(`[vision] f${p.frame} respondido (${resp.length} chars)`);
      }
    } else {
      const images = picked.map((p) => fs.readFileSync(p.dst).toString('base64'));
      const full = `Las ${picked.length} imágenes adjuntas van EN ORDEN TEMPORAL: ${legend}. ${prompt}`;
      const resp = await ask(images, full);
      out += `\n### Secuencia (${picked.map((p) => 'f' + p.frame).join(', ')}) — prompt\n\n\`\`\`text\n${full}\n\`\`\`\n\n### Secuencia — respuesta cruda\n\n${resp}\n`;
      console.log(`[vision] secuencia respondida (${resp.length} chars)`);
    }
  }
  if (conclusion) out += `\n## Conclusión de la pasada\n\n${conclusion}\n`;
  fs.writeFileSync(reportPath, (exists ? fs.readFileSync(reportPath, 'utf8') : '') + out);
  console.log(`[vision] informe: ${reportPath}`);
})();
