# Workspace por juego (`out/adventure/<juego>/`)

Todo el material **de trabajo** de cada juego (extracción, decodificación, edición, cocina, informes) vive aquí: `out/` es **gitignored** y regenerable, de modo que el repositorio nunca almacena contenido con derechos (assets, textos, guiones de los juegos originales). El material **canónico derivado** que sí entra en git son los documentos de `docs/reference/scumm/` y las herramientas de `tools/adventure/`.

## Estructura canónica

```text
out/adventure/<juego>/
├── game.json        descriptor del juego (id, motor, edición, ruta de la fuente, notas)
├── inbox/           material original de entrada (copias locales si se decide aislarlas; normalmente vacío)
├── raw/             extracción LITERAL del contenedor (solo la escriben los frontends)
│   ├── index/       rooms.json — SIM v1 (manifiesto de salas/recursos) y tablas del contenedor
│   ├── rooms/       room_<N>/{TAG}_<offset>.bin + blocks.json
│   │                (payload sin cabecera; el tag y el offset viven en blocks.json)
│   └── scripts/     room_<N>/{SCRP|EXCD|ENCD|LSCR}_<offset>.bin|.txt
│                    (.bin = bloque íntegro con cabecera, lo que consume descumm; .txt = descompilado)
├── decoded/         decodificación SEMÁNTICA de raw (la escriben los decodificadores)
│   ├── sim.json     SIM v2: salas con tamaño, walkboxes, paleta, objetos y guiones
│   ├── walkboxes/   room_<N>.json (BOXD+BOXM decodificados) + index.json
│   ├── png/         rooms/room_<N>.png y objects/room_<N>_obj<M>.png (indexados, sin pérdidas) + ../images.json
│   ├── audio/       00005.flac… (pistas del .sof sin recodificar) + manifest.json (adv-sof/1)
│   └── ast/         room_<N>/*.ast.json (guiones parseados, adv-ast/1) + index.json
├── edited/          FUENTE editable del port: SIM editado y añadidos Amiga (capas/parallax, transparencias, cues)
│   └── stages/      room_<N>.stage.json: formato nativo `adv-stage/1` (directivas de hardware A500)
├── cook/            salida COCINADA para el engine — regenerable desde decoded/edited
│   └── cpp/         <juego>_content.hpp (walkboxes/hotspots) + scripts/ (guiones C++ traducidos)
├── preview/         capturas e informes del previewer host (I3)
└── reports/         informes: opcode-usage.json/.md, cobertura, presupuesto (I4)
```

## Reglas

- **Quién escribe qué**: los frontends (`extract.mjs`) solo escriben en `raw/`; los decodificadores, en `decoded/`; el editor/previewer, en `edited/` y `preview/`; la cocina, en `cook/`; los análisis, en `reports/`. Nada pisa `edited/` de forma automática (es la fuente de trabajo del port y lleva revisión).
- **Regenerable**: borrar `raw/`, `decoded/`, `cook/` o `reports/` y volver a generarlos no debe perder trabajo humano; lo único irremplazable es `edited/`.
- **Determinismo**: mismos comandos → mismos bytes (los informes no llevan fechas; el workspace no versiona tiempos).
- **Nombres**: `<juego>` es un id corto estable (`mi1`, `mi2`, `foa`, `loom`, `indy3`…), aunque el contenedor se llame distinto (`monkey.000` → `mi1`); el descriptor guarda la ruta fuente.

## Relaciones entre artefactos

```text
datos del juego (.000/.00N)                        audio de la edición (.sof)
        │ extract.mjs                                     │ sof-extract.mjs
        ▼                                                  ▼
raw/{index,rooms,scripts} ──decode──► decoded/{sim.json, walkboxes/, png/, audio/}
        │                                   │
        │                                   ├─ translate/parse.mjs ──► decoded/ast/*.ast.json
        │                                   ├─ codegen/emit-content ──► cook/cpp/<juego>_content.hpp ──► engine
        │                                   └─ analyze/* ───────────► reports/*.json
        └────────────── edited/ (humano) ───► cook/ (futuro: UAF, planar, módulos)
```

| Artefacto | Se produce desde | Lo consume | Clave de enlace |
|---|---|---|---|
| `raw/rooms/room_<N>/<TAG>_<off>.bin` | contenedor `.00N` | `decode/` | `blocks.json` de la sala |
| `raw/scripts/room_<N>/*.txt` | bloques `SCRP/EXCD/ENCD/LSCR` | `translate/parse.mjs` | mismo basename → `.ast.json` |
| `decoded/sim.json` | `raw/rooms` (RMHD, BOXD/BOXM, CLUT, CDHD/IMHD) | `codegen/`, previewer (I3) | `room` (id) y `objects[].id` |
| `decoded/png/rooms/room_<N>.png` | bloque `RMIM` + paleta del SIM | cocina, visión, previewer | `sim.rooms[N]` (width/height/paleta) |
| `decoded/png/objects/room_<N>_obj<M>.png` | entrada `IMxx` del `OBIM` + paleta | cocina (BOBs/sprites), visión | `sim.rooms[N].objects` con `id=M` (x,y) |
| `decoded/audio/NNNNN.flac` | contenedor `.sof` | cocina de audio (I2) | `manifest.json` (`org`/`off`) |
| `decoded/ast/room_<N>/*.ast.json` | `.txt` de descumm | traductor I1, revisión | basename del guion |
| `edited/stages/room_<N>.stage.json` | andamio desde `decoded/sim.json` + edición humana | compositor A3, validador I4 | `room` y `layers[].content.ref` |
| `cook/cpp/scripts/room_<N>_scripts.hpp` | `decoded/ast` (traductor) | juego en el engine (`W` de `script_hooks.hpp`) | nombre `scrp_<sala>_<guion>` |
| `cook/cpp/<juego>_content.hpp` | `decoded/sim.json` | engine (`eng::adv`) | `find_room(id)` |
| `reports/*.json` | `raw/`, `decoded/` | revisión humana | — |

**Cada carpeta de artefactos lleva su propio `README.md` generado** (ficha local: qué es, quién lo produce y consume, y cómo se relaciona con el resto). Se reescribe al regenerar la carpeta; este documento es la referencia canónica del conjunto.

## Herramientas que tocan el workspace

| Paso | Herramienta | Escribe en |
|---|---|---|
| Scaffold + descriptor | `node tools/adventure/workspace.mjs init <juego> --source <ruta>` | `game.json`, carpetas |
| Extracción (V5) | `node tools/adventure/frontends/scumm_v5/extract.mjs dump-game <juego.000> --out out/adventure/<juego>` | `raw/` |
| Walkboxes | `... extract.mjs decode-boxes <juego.000> --out out/adventure/<juego>` | `decoded/walkboxes/` |
| SIM v2 semántico | `node tools/adventure/decode/scumm_v5-decode.mjs <juego>` | `decoded/sim.json` + `decoded/png/` + `decoded/images.json` |
| Audio (`.sof`) | `node tools/adventure/decode/sof-extract.mjs <juego>` | `decoded/audio/` (+`manifest.json`) |
| AST de guiones (I1) | `node tools/adventure/translate/parse.mjs <juego>` | `decoded/ast/` + `reports/parse-coverage.json` |
| Guiones C++ (I1) | `node tools/adventure/translate/emit.mjs <juego>` | `cook/cpp/scripts/` + `reports/translate-report.json` |
| Formato Amiga por sala | `node tools/adventure/formats/scaffold-stages.mjs <juego>` | `edited/stages/` |
| Cues de audio | `node tools/adventure/analyze/audio-cues.mjs <juego>` | `reports/audio-cues.json` |
| Contenido C++ | `node tools/adventure/codegen/emit-content.mjs <juego>` | `cook/cpp/<juego>_content.hpp` |
| Informes | `node tools/adventure/analyze/opcode-usage.mjs <juego>` | `reports/` |
| Estado | `node tools/adventure/workspace.mjs status` | — (stdout) |
