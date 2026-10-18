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
│   └── audio/       00005.flac… (pistas del .sof sin recodificar) + manifest.json (adv-sof/1)
├── edited/          FUENTE editable del port: SIM editado y añadidos Amiga (capas/parallax, transparencias, cues)
├── cook/            salida COCINADA para el engine — regenerable desde decoded/edited
│   └── cpp/         <juego>_content.hpp: tablas constexpr de eng::adv (codegen/emit-content.mjs)
├── preview/         capturas e informes del previewer host (I3)
└── reports/         informes: opcode-usage.json/.md, cobertura, presupuesto (I4)
```

## Reglas

- **Quién escribe qué**: los frontends (`extract.mjs`) solo escriben en `raw/`; los decodificadores, en `decoded/`; el editor/previewer, en `edited/` y `preview/`; la cocina, en `cook/`; los análisis, en `reports/`. Nada pisa `edited/` de forma automática (es la fuente de trabajo del port y lleva revisión).
- **Regenerable**: borrar `raw/`, `decoded/`, `cook/` o `reports/` y volver a generarlos no debe perder trabajo humano; lo único irremplazable es `edited/`.
- **Determinismo**: mismos comandos → mismos bytes (los informes no llevan fechas; el workspace no versiona tiempos).
- **Nombres**: `<juego>` es un id corto estable (`mi1`, `mi2`, `foa`, `loom`, `indy3`…), aunque el contenedor se llame distinto (`monkey.000` → `mi1`); el descriptor guarda la ruta fuente.

## Herramientas que tocan el workspace

| Paso | Herramienta | Escribe en |
|---|---|---|
| Scaffold + descriptor | `node tools/adventure/workspace.mjs init <juego> --source <ruta>` | `game.json`, carpetas |
| Extracción (V5) | `node tools/adventure/frontends/scumm_v5/extract.mjs dump-game <juego.000> --out out/adventure/<juego>` | `raw/` |
| Walkboxes | `... extract.mjs decode-boxes <juego.000> --out out/adventure/<juego>` | `decoded/walkboxes/` |
| SIM v2 semántico | `node tools/adventure/decode/scumm_v5-decode.mjs <juego>` | `decoded/sim.json` + `decoded/png/` + `decoded/images.json` |
| Audio (`.sof`) | `node tools/adventure/decode/sof-extract.mjs <juego>` | `decoded/audio/` (+`manifest.json`) |
| Contenido C++ | `node tools/adventure/codegen/emit-content.mjs <juego>` | `cook/cpp/<juego>_content.hpp` |
| Informes | `node tools/adventure/analyze/opcode-usage.mjs <juego>` | `reports/` |
| Estado | `node tools/adventure/workspace.mjs status` | — (stdout) |
