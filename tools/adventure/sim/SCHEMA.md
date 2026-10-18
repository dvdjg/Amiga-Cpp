# SIM v1 — Semantic Intermediate Model (esquema de extracción)

El **SIM** es el formato intermedio propio del pipeline de importación (la idea «AIR» de la consulta, ver [`ROADMAP_ADVENTURE_IMPORT.md`](../../../docs/guides/roadmap/ROADMAP_ADVENTURE_IMPORT.md)): un modelo semántico del juego original, independiente del motor de origen y ampliable con información Amiga (capas de scroll, transparencias, cues de Copper). Vive **solo en el host**; nunca se ejecuta en el Amiga.

`sim/1` es la versión actual y cubre lo que produce la **extracción** (inventario de recursos por sala). Las capas semánticas más ricas (walkboxes decodificadas, objetos con estados, textos, grafo de guiones) se añadirán en I0/I1 con **versión nueva** (`sim/2`), nunca cambiando `sim/1` en silencio.

## Estructura

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
- **Golden test**: `node --test tools/adventure/sim/sim.test.mjs` construye el fixture, lo extrae y compara con `test/golden-sim.json`; regenerar solo si el cambio es intencionado: `SIM_UPDATE_GOLDEN=1 node --test tools/adventure/sim/sim.test.mjs`.

## Relación con el resto del pipeline

- **Productor**: los frontends por familia (`../frontends/scumm_v5/extract.mjs` hoy; `scumm_v3v4` LFL, AGI, SCI y Cinématique después — I5).
- **Consumidores** (plan): traductor de guiones (`../translate/`, I1) que parte de los `.txt` de `descumm` + SIM; cocina de assets (`../../` I2); previewer Amiga-aware (I3); informe de presupuesto (I4).
