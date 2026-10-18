# Referencia SCUMM (para la conversión de aventuras)

Material de referencia del **pipeline de importación de aventuras** ([`ROADMAP_ADVENTURE_IMPORT.md`](../../guides/roadmap/ROADMAP_ADVENTURE_IMPORT.md)): cómo funciona el motor original que vamos a convertir y qué se traduce a la capa Amiga (`eng::adv`, ver [`ADVENTURE_LAYER.md`](../../engine/architecture/ADVENTURE_LAYER.md)).

Fuente primaria: el **código de ScummVM** como oráculo (fuente local `../SCUMM/scummvm-2026.4.0/`, ver «Fuentes locales» del roadmap) y las extracciones reales del pipeline (`out/adventure/<juego>/`, gitignored). Las fichas citan `engines/scumm/...` con fichero y línea.

| Documento | Contenido |
|---|---|
| [vm-v5.md](vm-v5.md) | La VM SCUMM V5 (Monkey Island 1/2): modelo de datos, bucle de juego, sentencias, opcodes y su uso real en MI1, y qué implican para el port Amiga |

Herramientas relacionadas: [`tools/adventure/`](../../../tools/adventure/README.md) — workspace por juego ([`WORKSPACE.md`](../../../tools/adventure/WORKSPACE.md)), frontend V5 ([`frontends/scumm_v5/README.md`](../../../tools/adventure/frontends/scumm_v5/README.md)), esquema SIM ([`sim/SCHEMA.md`](../../../tools/adventure/sim/SCHEMA.md)) y analizadores (`analyze/opcode-usage.mjs`).
