#!/usr/bin/env node
// Avisa si una cabecera *fundamental* del engine carece de diagrama ASCII de arquitectura.
// Norma: docs/engine/architecture/CODING_STYLE.md (seccion de documentacion de codigo).
//
// Por defecto NO falla (aviso); con --strict sale con codigo 1. La lista es deliberadamente
// corta: solo las piezas estructurantes (ownership, flujo de datos, jerarquias), no cada archivo.
import { readFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..');

const FUNDAMENTAL = [
  'engine/include/eng/core/span.hpp',
  'engine/include/eng/core/typed.hpp',
  'engine/include/eng/core/fixed.hpp',
  'engine/include/eng/core/mesh3d.hpp',
  'engine/include/eng/core/minifloat.hpp',
  'engine/include/eng/core/polygon.hpp',
  'engine/include/eng/memory/arena.hpp',
  'engine/include/eng/graphics/mode_switch.hpp',
  'engine/include/eng/graphics/composition/compose.hpp',
  'engine/include/eng/graphics/polygon_planes.hpp',
  'engine/include/eng/field/raster.hpp',
  'engine/include/eng/graphics/palette32.hpp',
  'engine/include/eng/graphics/composition/limits.hpp',
  'engine/include/eng/retro/flat_shade_xor.hpp',
  'engine/include/eng/graphics/copper/copper.hpp',
  'engine/include/eng/graphics/copper/scheduler.hpp',
  'engine/include/eng/graphics/copper/plan.hpp',
  'engine/include/eng/graphics/copper/double_buffer.hpp',
  'engine/include/eng/graphics/copper/timeline.hpp',
  'engine/include/eng/graphics/copper/static_plan.hpp',
  'engine/include/eng/field/surface.hpp',
  'engine/include/eng/field/playfield.hpp',
  'engine/include/eng/field/xlimited.hpp',
  'engine/include/eng/field/streaming_map.hpp',
  'engine/include/eng/ai/decision/blackboard.hpp',
  'engine/include/eng/ai/decision/behavior_tree.hpp',
  'engine/include/eng/ai/decision/utility.hpp',
  'engine/include/eng/ai/navigation/navmesh_lite.hpp',
  'engine/include/eng/sim/colony.hpp',
  'engine/include/eng/graphics/frame_plan.hpp',
  'engine/include/eng/graphics/bitmap.hpp',
  'engine/include/eng/graphics/sprite_manager.hpp',
  'engine/include/eng/field/xlimited_scene.hpp',
  'engine/include/eng/ai/perception/influence_map.hpp',
];

// Cualquier caracter de caja/flecha de un diagrama ASCII cuenta como "tiene diagrama".
const DIAGRAM = /[\u2500-\u257F\u25b2\u25bc\u25ba\u25c4]/;

let missing = 0;
for (const rel of FUNDAMENTAL) {
  const abs = join(root, rel);
  if (!existsSync(abs)) {
    console.log(`[diagrams] falta el archivo: ${rel}`);
    missing += 1;
    continue;
  }
  const head = readFileSync(abs, 'utf8').split('\n').slice(0, 120).join('\n');
  if (!DIAGRAM.test(head)) {
    console.log(`[diagrams] sin diagrama ASCII: ${rel}`);
    missing += 1;
  }
}

if (missing === 0) {
  console.log(`[diagrams] OK: las ${FUNDAMENTAL.length} cabeceras fundamentales tienen diagrama`);
} else {
  console.log(`[diagrams] AVISO: ${missing} cabecera(s) fundamental(es) sin diagrama ASCII`);
  if (process.argv.includes('--strict')) {
    process.exit(1);
  }
}
