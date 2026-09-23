# Roadmap — generación procedural de mundos (metroidvania)

Plan para generar **mundos interconectados estilo metroidvania** (biomas, cavernas y superficie,
puertas con llave, habilidades que abren caminos, desafíos y objetos) sin editor, con la restricción
de **Amiga 500** (Chip RAM escasa, 68000 sin MMU). El material de partida es el documento
«Generación Procedural de Mundos Amiga» (grafo + autómata celular + solvencia + empaquetado RLE +
mecanismos + topología de superficies); este roadmap lo **mejora y lo adapta al engine**: se
**reutiliza** lo que ya existe (`eng::sim`, `eng::core::noise`, `eng::util::{graph,pathfinding}`,
`eng::field`, `WORLD_FORMAT`, ZX0) y se define **solo lo que falta**.

## Principios (reglas del engine)

- **Sin heap en el camino caliente.** Pools de capacidad fija (`eng/core/util/pool.hpp`) y árboles
  intrusivos, como el resto del engine. El `std::vector`/`std::unordered_map` del documento de
  origen **no** valen en el 68000: se sustituyen por `Pool`, `SparseTileMap`, `Graph` y `Span`.
- **Determinismo total.** Un único `eng::Xoroshiro64pp` sembrado (`eng/core/random.hpp`); **misma
  semilla → mismo mundo** en host y en Amiga. Nada de `std::mt19937_64` (no reproducible ni
  portátil) ni de `float` en la generación que corre en Amiga.
- **El mundo es datos, no código.** La generación **emite** el modelo que ya consumen `eng::sim`
  (`set_biome`/`set_region`/`add_link`) y `eng::field` (`TileLayerMap`/`WorldMap`); no crea tipos
  paralelos de bioma/terreno.
- **Reutilizar antes de crear** (`AGENTS.md` §1.6): biomas, clima, terreno y su semántica **ya
  existen** (`eng/sim/`); grafo, BFS y A\* **ya existen** (`eng/util/`); ruido y RNG **ya existen**
  (`eng/core/`). El roadmap **no** reimplementa ninguno.
- **HOST primero.** Toda la generación es **pura** (sin hardware) → test host bloque D; la demo solo
  dibuja el resultado.
- **Dos backends de generación.** El **mismo** `WorldGen` corre en **host** (precocinado, dentro del
  pipeline `tools/ehb`/`WORLD_FORMAT`) o en **Amiga runtime** (semilla + regeneración en `init`).
  No se elige: se soportan ambos con el mismo código puro.

## Qué reutiliza (inventario real)

| Pieza | Dónde | Para qué |
|---|---|---|
| Biomas y perfiles | `eng/sim/biome.hpp:26` (`BiomeKind`), `:53` (`biome_profile`) | temática por región; **no** crear `BiomeType` propio |
| Terreno semántico | `eng/sim/terrain.hpp:31` (`TerrainKind`), `:69` (`can_traverse`), `:197` (`TerrainMap<W,H>`) | superficies (agua/trepar/hueco/cobertura) para física y pathfinding |
| Clima/exposición | `eng/sim/climate.hpp:46`, `:112` | gradientes ambientales por región |
| Grafo macro + BFS de salas | `eng/sim/world.hpp:943` (`route_room`), `add_link` (~`:1242`) | topología del mundo |
| Ruido procedural | `eng/core/noise.hpp:211` (`fbm2`), `:319` (`worley2`), `:352` (`ridged2`) | alturas/biomas/cavernas con **coherencia espacial** |
| RNG determinista | `eng/core/random.hpp:40` (`Xoroshiro64pp`), `:101` (`chance`), `:119` (`shuffle`) | todo lo aleatorio |
| Grafo genérico + A\* | `eng/core/util/graph.hpp:199` (`graph_astar`), `:162` (`graph_bfs`) | solvencia y rutas macro |
| Rejilla BFS/A\* | `eng/core/util/pathfinding.hpp:109` (`bfs<W,H>`), `:162` (`astar<W,H>`) | caminos en la rejilla de sala |
| Flow field / navmesh | `eng/ai/navigation/flow_field.hpp:66`, `navmesh_lite.hpp` | navegación de NPCs (ya existentes) |
| Mapa de tiles | `eng/field/tile_map.hpp:20` (`TileLayerMap`), `tile_source.hpp:74` (`SparseTileMap<16>`) | salida de la geometría |
| Chunks + streaming | `eng/field/chunk_cache.hpp:68`, `streaming_map.hpp:64` | carga bajo demanda |
| Formato de mundo | `docs/engine/architecture/WORLD_FORMAT.md`, `eng/assets/uaf.hpp` (`WorldView`) | contenedor `WorldMap` (celdas `u16` banco) |
| Compresión | `eng/audio/zx0.hpp:71` (`decompress`), `support/depacker_doynax.s` | empaquetar geometría/atributos |
| Pool / capacidad fija | `eng/core/util/pool.hpp:28` (`Pool<T,N>`, `Handle`) | salas, entidades, mecanismos |
| Tablero/RNG de cartas | `eng/cards/core/deck.hpp:53` (`shuffle(Xoroshiro64pp&)`) | ejemplo de determinismo ya validado |

**Lo que NO existe (a construir):** el **generador de layout** (grafo + gating + geometría), la
**solvencia**, la **colocación de entidades/mecanismos**, la **topología de superficies por celda** y
la **física de actores** (gravedad/nadar/escalar). Este roadmap los cubre.

## Mejoras sobre el documento de origen

1. **Ruido real, no `ratio = i/total`.** El documento asigna bioma por índice lineal
   (`determine_biome`) → mundos por «franjas». Se usa `fbm2`/`worley2` sobre **coordenadas de región**
   → biomas contiguos, cavernas con `worley` y crestas con `ridged`.
2. **Grafo con forma, no aleatorio puro.** Un árbol aleatorio (`parent = rand(0..i-1)`) produce
   topologías sin geografía. Se genera sobre una **rejilla de regiones** (grafo plano con vecinos 4/8)
   y se conectan subconjuntos + **aristas extra** para hacer bucles (metroidvania **no** es un árbol:
   los atajos son progresión).
3. **Solvencia por progreso, no BFS de estados con 64 bits.** El documento usa
   `unordered_set<(sala, bitmask)>` (hasta 64 objetos, inviable en 68000 sin heap). Se usa un
   **BFS de *frontera de objetos***: iterar «alcanzable con el inventario actual → recoger → repetir»
   hasta punto fijo. Es **O(nº objetos × nº salas)**, memoria fija y **sin dedup por estado**.
4. **Gating garantizado, no reparación a posteriori.** El documento «rebaja cerraduras» si falla
   (`SolvabilityFixer`). Mejor: **colocar cada llave ANTES de su cerradura por construcción** (orden
   topológico del árbol de progresión) → nunca hay que reparar.
5. **Empaquetado alineado al engine, no `#pragma pack`.** En vez de bitfields de longitud variable
   (endianness y ABI frágiles), usar **máscaras de bits explícitas** en `u16`/`u8` (big-endian
   Amiga) y `static_assert` de tamaño, coherente con `WORLD_FORMAT`.
6. **Topología de superficies como máscara, no `switch`.** El `enum TileAttributeFlags` del
   documento **ya encaja** con el engine: es exactamente un `u8` por celda + AND bitwise para
   `can_actor_enter_cell`. Se reutiliza la idea y se ata a `TerrainKind` (`eng/sim`) para no duplicar
   semántica.
7. **Mecanismos como datos + sistema, no `struct` con `std::vector`.** `PackedMechanism` compacto
   (6 B) + un **sistema de actualización** (`update_mechanisms`) que opera sobre el pool, sin clases
   por mecanismo.
8. **Salida doble (host/Amiga) y por chunks** — no un blob monolítico: `WorldMap` por chunks permite
   **streaming** (`StreamingWorldMap`) y reutiliza el pipeline de tiles existente.

## Arquitectura objetivo

```text
  semilla (u64) ──► WorldGen (puro, host-testable; sin heap en Amiga)
                       │
   ┌───────────────────┼───────────────────────────────────────────────┐
   │ 1 región/bioma    │ 2 topología (grafo)  3 layout+gating     4 geometría
   │ fbm2 + BiomeKind  │ Graph<MaxRooms>      BFS/A* + progresión  autómata + ruido
   └───────────────────┴───────────────────────────────────────────────┘
                       │                                    │
                       ▼                                    ▼
        SimWorld::set_biome/set_region/add_link      TileLayerMap / WorldMap (u16 banco)
                       │                                    │
                       └──────────► WorldGenOutput ─────────┘
                                       │
                   5 entidades/mecanismos (Pool)   6 topología de superficies (u8/celda)
                                       │
                                       ▼
                        demo: dibuja sala + física de actor
```

## Fases

### W0 — Núcleo determinista (`eng/world/` o `eng/sim/gen/`)

- **Entregable**: `WorldGen` puro que emite `WorldGenOutput` (regiones con `BiomeKind`,
  `Graph<MaxRooms,MaxEdges>`, semillas por sala) a partir de **una semilla** (`Xoroshiro64pp`) y
  **sin heap** (pools/`ct_array`/`Span`). Reutiliza `eng/sim/biome.hpp` y `eng/util/graph.hpp`.
- **Verificación**: **HOST-277** — misma semilla → mismo grafo (byte a byte); semillas distintas →
  grafos distintos; nº de salas y conectividad dentro de cotas; sin asignación dinámica (contadores
  de arena).
- **Estado**: pendiente.

### W1 — Regiones y biomas con ruido

- **Entregable**: asignación de `BiomeKind` por región usando `fbm2`/`worley2` (coordenadas de
  región), gradiente de peligro inicial; emite `SimWorld::set_biome`/`set_region`.
- **Verificación**: **HOST** — biomas **contiguos** (un region y sus vecinos tienden al mismo bioma),
  no franjas; cobertura de todos los biomas; determinismo.
- **Estado**: pendiente.

### W2 — Topología metroidvania (grafo con bucles)

- **Entregable**: grafo sobre rejilla de regiones + conectividad garantizada (BFS) + aristas extra
  (atajos); tipos de transición (`OpenPath`, `DoorRequiresKey`, `WallRequiresAbility`, `OneWayDrop`).
- **Verificación**: **HOST** — grafo **conexo**; hay ciclos (atajos); grados dentro de cotas del pool.
- **Estado**: pendiente.

### W3 — Layout, progresión y colocación de llaves

- **Entregable**: elegir en el grafo un **árbol de progresión** desde el spawn; orden **topológico**
  de habilidades; colocar cada **llave/habilidad antes** de la cerradura que abre; marcar objetivos y
  salas de jefe por **excentricidad** (distancia al spawn, ver W5).
- **Verificación**: **HOST** — solvencia **por construcción** (ver W4) para N semillas; ninguna llave
  queda tras su propia cerradura.
- **Estado**: pendiente.

### W4 — Análisis de solvencia (frontera de objetos)

- **Entregable**: `is_solvable(output)`: BFS de **frontera de inventario** (alcanzable con inventario
  actual → recoger → repetir hasta punto fijo), sin dedup por estado. Reutiliza `eng/util/graph.hpp`.
- **Verificación**: **HOST** — un mundo **insoluble** inyectado a mano se detecta; el generado de W3
  es soluble; coste O(objetos × salas) medido.
- **Estado**: pendiente.

### W5 — Distancia/peligro y colocación de entidades

- **Entregable**: `danger_rating` por `graph_bfs` desde el spawn (normalizado); colocación de
  entidades (spawn, guardado, cofres, enemigos, jefes) en **celdas válidas** de la geometría (suelo
  sólido debajo, espacio libre, camino despejado) con `Pool` y footprint de actor.
- **Verificación**: **HOST** — las entidades no caen en sólido/pinchos ni solapan; jefes en extremos;
  guardado en zonas de peligro medio.
- **Estado**: pendiente.

### W6 — Geometría por sala (autómata + ruido)

- **Entregable**: `LocalRoomGrid` generada por **autómata celular 4-5** con semilla por sala,
  bordes garantes de conexión (las salidas del grafo deben ser **alcanzables** intra-sala), relleno
  guiado por el bioma; salida como `TileLayerMap`/`WorldMap` (celdas `u16`).
- **Verificación**: **HOST** — bordes sólidos; las puertas del grafo conectan con espacio libre; la
  sala es **conexa** (BFS sobre la rejilla, `pathfinding.hpp`).
- **Estado**: pendiente.

### W7 — Topología de superficies por celda

- **Entregable**: máscara de **atributos `u8` por celda** (walkable/climbable/swimmable/fly/hazard/
  one-way/hide) `[4 bits]` + `material_table[16]` (fricción/velocidad), **atada a `TerrainKind`**;
  consulta por AND bitwise (`can_enter(cell, capability)`). Compresión RLE/ZX0.
- **Verificación**: **HOST** — cada capacidad filtra correctamente; la máscara comprime bien (áreas
  homogéneas).
- **Estado**: pendiente.

### W8 — Mecanismos y zonas de interés (análisis semántico)

- **Entregable**: `SpatialTopologyAnalyzer` (pozos, abismos, techos abiertos, alcobas, pasadizos
  verticales) + `AIStructuralIntegrator` que coloca **mecanismos compactos** (`PackedMechanism`, 6 B)
  por reglas de bioma/peligro; **no** por `switch` en runtime.
- **Verificación**: **HOST** — cada mecanismo cae en su zona apta (plataforma móvil en abismo,
  líquido en pozo, cuerda en techo, pasadizo oculto en alcoba); sin solapes.
- **Estado**: pendiente.

### W9 — Física de actores multi-modo (gravedad/nadar/escalar/volar)

- **Entregable**: capa **nueva** (no existe hoy) que consume W7: estados `Walk/Run`, `Swim`, `Climb`,
  `Fly` con `TerrainKind` + `material_table`; gravedad, flotabilidad, fricción; colisión con
  `eng/core/util/collision.hpp` (`Aabb`) y `broadphase.hpp` (`SpatialHash`); input por
  `eng/input/input.hpp`.
- **Verificación**: **HOST** — un actor cae, se frena en hielo, flota en agua, se agarra a una cuerda
  y vuela atravesando vacío; y demo que lo muestra.
- **Estado**: pendiente. **Base**: `3D_PHYSICS.md` (§ estados) como diseño.

### W10 — Persistencia (empaquetado + compresión)

- **Entregable**: serializar `WorldGenOutput` al contenedor del engine (cabecera + regiones + grafo +
  geometría RLE + atributos RLE + entidades/mecanismos), con **máscaras explícitas** big-endian y
  `static_assert` de tamaño; **ZX0** (`eng/audio/zx0.hpp`) para la geometría; integración con
  `WORLD_FORMAT`/`WorldView`.
- **Verificación**: **HOST** — round-trip byte a byte; el contenedor lo lee `WorldView`; presupuesto
  de RAM por mundo tabulado.
- **Estado**: pendiente.

### W11 — Integración con la simulación y el render

- **Entregable**: volcar el mundo a `SimWorld` (`set_biome`/`set_region`/`add_link`, NPCs) y a
  `eng::field` (`TileLayerMap`/`StreamingWorldMap`); demo que **navega** el mundo generado (cámara,
  scroll, un NPC patrullando con `flow_field`).
- **Verificación**: **demo** (bloque D) — mundo generado navegable, biomas visibles, un NPC usa la
  topología; `analyze` OK.
- **Estado**: pendiente. **Depende de**: W0–W9.

### W12 — Generación en Amiga (runtime) vs host (precocinado)

- **Entregable**: el **mismo** `WorldGen` compilado para 68000 (sin `float` en la ruta crítica:
  ruido y gating en **punto fijo**/enteros) + comando en el pipeline host
  (`tools/ehb/pack-world.mjs`) para precocinar y empaquetar; decisión por coste/fps medida.
- **Verificación**: **HOST** (Amiga) — genera un mundo en `init` dentro del presupuesto de un frame de
  carga; **demo** — mundo generado en runtime y jugable.
- **Estado**: pendiente.

## Tests y demos previstos

| ID | Tipo | Contenido |
|---|---|---|
| HOST-277 | test | Núcleo determinista (`WorldGen`, semilla→mundo, sin heap). |
| HOST-278 | test | Regiones/biomas con ruido (contigüidad, cobertura, determinismo). |
| HOST-279 | test | Topología (grafo conexo + ciclos/atajos). |
| HOST-280 | test | Solvencia por frontera de inventario (+ caso insoluble inyectado). |
| HOST-281 | test | Geometría por sala (bordes, conexidad intra-sala, puertas alcanzables). |
| HOST-282 | test | Topología de superficies + física multi-modo. |
| HOST-283 | test | Mecanismos (zona apta por tipo, sin solapes). |
| HOST-284 | test | Persistencia (round-trip + presupuesto de RAM). |
| demo | demo | `277_<tema>` (bloque D): mundo generado, navegable, biomas + NPC. |

Números reservados del bloque D; `node tools/check/next-number.mjs` da el siguiente libre.

## Decisiones abiertas

- **`eng/world/` nuevo vs `eng/sim/gen/`.** El generador *produce* el modelo de `eng::sim`; como es
  lógica pura de simulación, encaja en `eng/sim/gen/` (evita una capa nueva). Alternativa: `eng/world/`
  si crece con render/persistencia. **Decidir en W0.**
- **Tipo de sala.** ¿La rejilla de sala es fija (p. ej. 32×16 celdas) o variable? Fija simplifica el
  empaquetado y el pooling; variable da variedad. **Propuesta**: fija + «macro-celdas» de 2×2 para
  salas grandes.
- **Generar en runtime o precocinar.** W12 lo decide con medida; por defecto **precocinar** en host
  (no gasta frame de juego) y dejar runtime para prototipado/semillas por partida.
- **Punto fijo para el ruido.** `eng/core/noise.hpp` es genérico sobre escalar: usar `MiniFloat16`/
  `Fixed` en Amiga y `float` en host; verificar que la **ramificación de bioma** coincide en ambos
  (misma semilla → mismo mundo).

## Riesgos

- **Solvencia y progresión**: garantizar gating por construcción (W3) es lo que evita el «softlock»;
  el análisis (W4) es la red de seguridad, no el mecanismo principal.
- **Coste CPU de la generación en 68000**: el autómata + ruido por sala pueden no caber en un frame;
  mitigar con generación **por sala bajo demanda** (`ChunkCache`) y precocinado.
- **Presupuesto de RAM**: cada pieza nueva (atributos de superficie, mecanismos) suma por celda;
  medir con la tabla de W10 y comprimir (RLE/ZX0).
- **Coherencia host/Amiga**: cualquier `float` o `std::` en la ruta de generación rompe el
  determinismo cruzado; lint de estilo (el gate `generic-headers` ayuda).

## No-objetivos

- **No** es un editor de niveles: el mundo nace de la semilla.
- **No** reimplementa `eng::sim` (biomas/clima/terreno) ni `eng::util` (grafo/A\*) ni el pipeline de
  tiles/EHB: los **consume**.
- **No** promete «Ori/Hollow Knight» (arte y diseño a mano): la meta es **topología y mecánicas**
  ricas y jugables en A500, con arte procedural/placeholder.

## Referencias

- Documento de origen: «Generación Procedural de Mundos Amiga» (aporta: grafo + CA + solvencia + RLE +
  mecanismos + topología de superficies). **Mejorado aquí** en ruido, topología con bucles, solvencia
  por progreso, gating por construcción, empaquetado alineado y doble backend.
- `exile_disassembly` / `ExileWorldGenerator` (inspiración conceptual; 32 KB).
- Engine: [`SIM_ECOSYSTEM.md`](../../engine/architecture/SIM_ECOSYSTEM.md),
  [`WORLD_FORMAT.md`](../../engine/architecture/WORLD_FORMAT.md),
  [`CONTENT_AND_TILEMAP.md`](../../engine/architecture/CONTENT_AND_TILEMAP.md),
  [`STREAMING_LOADER.md`](../../engine/architecture/STREAMING_LOADER.md),
  [`3D_PHYSICS.md`](../../engine/architecture/3D_PHYSICS.md) (física por estados, diseño).
- Código: `eng/sim/{biome,terrain,climate,world}.hpp`, `eng/core/{noise,random}.hpp`,
  `eng/core/util/{graph,pathfinding,pool}.hpp`, `eng/field/{tile_map,chunk_cache,streaming_map}.hpp`,
  `eng/ai/navigation/{flow_field,navmesh_lite}.hpp`, `eng/audio/zx0.hpp`.
