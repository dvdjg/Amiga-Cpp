#!/usr/bin/env node
// Pasada de visión de una demo: copia los frames con NOMBRE CANÓNICO a
// `out/run/<demoId>/<config>/vision/` y escribe/agrega el informe crudo
// `out/run/<demoId>/<config>/<demoId>_report.md`. Convención completa en
// docs/guides/methodology/DEMO_VISUAL_DEBUG.md §6.5.
//
// Uso:
//   node tools/analyze/vision-run.mjs <demoId> <config> <dirSeq> <idx…>
//        --prompt "…" [--prompt "…"] [--model m] [--conclusion "…"]
//
// - `idx` = número de la secuencia (frame_<idx>_f<frame>.png); una imagen por
//   llamada (§6.4: el modelo no atiende varias imágenes de forma fiable).
(async () => {
  const fs = await import('node:fs');
  const path = await import('node:path');
  const { spawn } = await import('node:child_process');

  const argv = process.argv.slice(2);
  const positional = [];
  const prompts = [];
  let model = 'qwen3-vl:8b-instruct-q8_0';
  let conclusion = '';
  for (let i = 0; i < argv.length; ++i) {
    const a = argv[i];
    if (a === '--prompt') prompts.push(argv[++i]);
    else if (a === '--model') model = argv[++i];
    else if (a === '--conclusion') conclusion = argv[++i];
    else positional.push(a);
  }
  const [demoId, config, seqDir, ...idxRaw] = positional;
  const idxs = idxRaw.map((v) => parseInt(v, 10)).filter((v) => Number.isFinite(v));
  if (!demoId || !config || !seqDir || idxs.length === 0 || prompts.length === 0) {
    console.error('uso: vision-run.mjs <demoId> <config> <dirSeq> <idx…> --prompt "…" [--prompt "…"] [--conclusion "…"]');
    process.exit(2);
  }

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
  const visionDir = path.join('out', 'run', demoId, config, 'vision');
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

  // ---- Informe ----
  const reportPath = path.join('out', 'run', demoId, config, `${demoId}_report.md`);
  let out = '';
  const exists = fs.existsSync(reportPath);
  if (!exists) {
    out += `# Informe de ejecución — ${demoId} (${config})\n\n`;
    out += `Crudo de las pasadas de visión/medida. Convención: docs/guides/methodology/DEMO_VISUAL_DEBUG.md §6.5.\n`;
    out += `El resumen canónico commiteado vive en el VALIDATION.md de la demo.\n`;
  }
  out += `\n## Pasada ${new Date().toISOString()}\n\n`;
  out += `- Secuencia: \`${seqDir}\`\n- Frames analizados: ${picked.map((p) => `f${p.frame}`).join(', ')} (capturas: \`${visionDir.replaceAll('\\', '/')}/${demoId}_f*.png\`)\n`;
  out += `- Modelo: ${model}\n`;

  for (const p of picked) {
    const b64 = fs.readFileSync(p.dst).toString('base64');
    for (const prompt of prompts) {
      const body = { model, prompt, images: [b64], stream: false };
      const r = await fetch(`${BASE}/api/generate`, {
        method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body),
        signal: AbortSignal.timeout(300000),
      });
      const j = await r.json();
      out += `\n### f${p.frame} — prompt\n\n\`\`\`text\n${prompt}\n\`\`\`\n\n### f${p.frame} — respuesta cruda\n\n${(j.response ?? '').trim()}\n`;
      console.log(`[vision] f${p.frame} respondido (${(j.response ?? '').length} chars)`);
    }
  }
  if (conclusion) out += `\n## Conclusión de la pasada\n\n${conclusion}\n`;
  fs.writeFileSync(reportPath, (exists ? fs.readFileSync(reportPath, 'utf8') : '') + out);
  console.log(`[vision] informe: ${reportPath}`);
})();
