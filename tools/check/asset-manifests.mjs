#!/usr/bin/env node
// Gate de **sincronización del manifiesto de assets** (ROADMAP_GAME_API.md §4).
//
// Todo `assets.manifest.json` de `demos/` y `games/` debe tener su `assets.manifest.hpp` hermano
// **regenerado** (nada de editar el header a mano). Falla si hay un header desincronizado o
// ausente. La lógica de generación vive en `tools/assets/gen-manifest.mjs` (modo `--check`), no se
// duplica aquí.
import * as fs from 'fs';
import * as path from 'path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const GEN = path.join(ROOT, 'tools/assets/gen-manifest.mjs');

function findManifests(dir, out = []) {
  if (!fs.existsSync(dir)) return out;
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) findManifests(p, out);
    else if (e.name === 'assets.manifest.json') out.push(p);
  }
  return out;
}

const manifests = [...findManifests(path.join(ROOT, 'demos')), ...findManifests(path.join(ROOT, 'games'))];
if (manifests.length === 0) {
  console.log('[asset-manifests] sin manifiestos `assets.manifest.json`; nada que comprobar.');
  process.exit(0);
}

let bad = 0;
for (const json of manifests) {
  const r = spawnSync(process.execPath, [GEN, path.relative(ROOT, json), '--check'], {
    cwd: ROOT,
    encoding: 'utf8',
  });
  process.stdout.write(r.stdout || '');
  process.stderr.write(r.stderr || '');
  if (r.status !== 0) bad++;
}

if (bad > 0) {
  console.error(`[asset-manifests] ${bad} manifiesto(s) DESINCRONIZADO(S).`);
  process.exit(1);
}
console.log(`[asset-manifests] OK: ${manifests.length} manifiesto(s) sincronizados.`);
