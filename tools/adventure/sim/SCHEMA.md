# SIM — Semantic Intermediate Model (esquemas `sim/1` y `sim/2`)

El **SIM** es el formato intermedio propio del pipeline de importación (la idea «AIR» de la consulta, ver [`ROADMAP_ADVENTURE_IMPORT.md`](../../../docs/guides/roadmap/ROADMAP_ADVENTURE_IMPORT.md)): un modelo semántico del juego original, independiente del motor de origen y ampliable con información Amiga (capas de scroll, transparencias, cues de Copper). Vive **solo en el host**; nunca se ejecuta en el Amiga.

`sim/1` es la versión de **inventario de extracción** (salas y bloques). `sim/2` añade la capa **semántica** decodificada (tamaño de sala, walkboxes con adyacencia, paleta, objetos con hotspots de imagen y guiones por sala) y es la que consumen el generador de contenido y los pasos siguientes.

## `sim/2` (semántico, producido por `tools/adventure/decode/`)

```text
SIM v2 {
  format: "sim/2"
  game  { id, engine, source, counts }
  rooms [ {
    room, name, file
    width, height                 // RMHD (u16 LE, píxeles)
    walkboxes [ { id, ul, ur, lr, ll, mask, flags, scale } ]   // BOXD (coords s16, scale 255=100%)
    adjacency [[ ids ]] | null    // BOXM (runs de vecinos por caja; terminador 0xFF)
    palette { kind:"clut", colors:[[r,g,b]×256] } | { kind:"epal", entries:[256] } | null
    objects [ { id, x, y, width, height, flags, parent, walkX, walkY, actordir,
                image?: { width, height, imageCount, hotspots:[{x,y}] } } ]  // CDHD + IMHD
    scripts [ { tag, size, file } ]      // refs a raw/scripts/room_N/
  } ]
}
```

Reglas adicionales de v2: coordenadas de objeto ya en píxeles (×8, como `resetRoomObject`); los hotspots de imagen se conservan (máx. 15 por objeto); los `file` son rutas **relativas al workspace**. Validación: `validateSim2` en `sim.mjs`; golden del fixture en `tools/adventure/decode/test/`.

## `sim/1` (inventario, producido por el frontend)

```text
SIM {
  format: "sim/1"
  game {
    id: string                    // nombre base del índice (p. ej. "monkey2")
    source { type, index, data[], enc }   // p. ej. type="scumm-v5", enc=105 (0x69)
    counts { rooms, scripts, costumes, sounds, charsets, globalObjects }
  }
  roomNames { "<sala>": "<nombre>" }      // del bloque RNAM (puede faltar alguna)
  rooms [ {
    room: u16 > 0                 // número de sala lógico
    name: string
    file: string                  // fichero de datos donde vive (basename)
    off: u32                      // offset del bloque ROOM
    lflfOff: u32                  // offset del LFLF contenedor
    blocks [ { tag: "RMHD"|..., size } ]      // hijos del ROOM
    lflfBlocks [ { tag, size } ]  // hijos del LFLF fuera del ROOM (guiones, COST/CHAR/SOUN)
    scripts [ { tag: "SCRP"|"EXCD"|"ENCD"|"LSCR", off, size } ]  // subconjunto de los dos anteriores
  } ]
}
```

## Reglas

- **Determinista**: mismas entradas → mismos bytes de JSON (orden de claves fijo, sin rutas absolutas, sin timestamps).
- **Validable**: `sim.mjs` (`validateSim(sim) → string[]`) comprueba tipos, duplicados de sala y formas de los bloques; el extractor y los tests lo usan.
- **Con derechos fuera del repo**: un SIM de un juego comercial se genera en `out/` (gitignored). Al repositorio solo entran el esquema, las herramientas y el **fixture sintético** (`make-fixture.mjs`).
- **Golden tests**: `node --test tools/adventure/sim/sim.test.mjs` (v1), `node --test tools/adventure/decode/decode.test.mjs` (v2) y `node --test tools/adventure/codegen/codegen.test.mjs` (contenido C++); cada uno construye el fixture sintético, ejecuta su etapa y compara con su golden; regenerar solo si el cambio es intencionado: `SIM_UPDATE_GOLDEN=1 node --test <fichero>`.

## Relación con el resto del pipeline

- **Productores**: los frontends por familia producen `sim/1` (`../frontends/scumm_v5/extract.mjs` hoy; `scumm_v3v4` LFL, AGI, SCI y Cinématique después — I5); `../decode/scumm_v5-decode.mjs` deriva `sim/2` leyendo `raw/`.
- **Consumidores**: el generador de contenido `../codegen/emit-content.mjs` (SIM v2 → tablas `constexpr` de `eng::adv`, `cook/cpp/`); y los previstos: traductor de guiones (`../translate/`, I1) que parte de los `.txt` de `descumm` + SIM; cocina de assets (I2); previewer Amiga-aware (I3); informe de presupuesto (I4).
