# Roadmap de motores de juegos de tablero (`eng::board`)

Plan de crecimiento de `engine/include/eng/board/`: motores de **ajedrez** y **Go 9×9** para
Amiga 500–1200 (68000/68020/68030) con **footprint de 20 kB a 1 MB** y **conocimiento en
almacenamiento externo** (disquete/HD). El diseño vigente (capas, estructuras, tablas de
presupuesto, búsqueda, reglas, evaluación y NLG) está en
[BOARD_GAME_AI.md](../../engine/architecture/BOARD_GAME_AI.md) (fuente de referencia, no se
duplica aquí). Este documento es la **fuente única del avance**: qué falta, en qué orden y cómo
se verifica.

Relación con la IA de videojuego: `eng::ai` ([GAME_AI_LIBRARY.md](../../engine/architecture/GAME_AI_LIBRARY.md),
[ROADMAP_GAME_AI.md](ROADMAP_GAME_AI.md)) cubre tiempo real; `eng::board` cubre búsqueda
adversaria por turnos. Ambas reutilizan `eng::util`/`eng::math` y no se duplican.

## 1. Objetivo y criterio

Dotar al engine de motores de tablero **legales y usables** en hardware de 1987–1992, con
comportamiento moderno pese a los algoritmos clásicos: búsqueda que **nunca se detiene**
(iterative deepening + pondering), puntuación táctica y estratégica visible (Multi-PV),
conocimiento consultado en disquete para no inflar la RAM, y explicación de la posición en
lenguaje natural (ES/EN). Cada pieza respeta las reglas transversales de §3 y se cierra con
test host, cruce `m68k` y, cuando corresponde, un juego en `games/`.

## 2. Estado de partida

- **Implementado**: núcleo de `eng::board` (`core/`: tipos 0x88, Zobrist, `GameRules`, budget),
  reglas de ajedrez (`rules/chess/`: tablero, `make`/`unmake`, generación legal, FEN, SAN,
  repetición y finales), búsqueda adversaria (`search/`: negamax/αβ, ID, quiescence, TT,
  null-move, PV/Multi-PV) y evaluación (`eval/`). Verificado por HOST-138…145 y HOST-148.
- **Implementado (conocimiento)**: `storage/BlockSource` (concept, tres estados, fuente RAM
  `RamBlockSource`, fuente de fichero del PC `FileBlockSource`) y `knowledge/` (caché LRU, libro
  de aperturas y tablas de finales), con round-trip por bloque; HOST-146/147/151. Packer de
  libro `tools/board/pack-book.sh` (texto → blob). Los **backends de disquete Amiga y los
  packers de tablas/patrones** quedan pendientes.
- **Implementado (NLG)**: explicador por plantillas ES/EN (`explain/`), HOST-149.
- **Primer consumidor**: `games/100_chess` verificado en emulador con build → run → analyze
  (tablero + reglas + búsqueda + explicación); pulido visual pendiente.
- **Implementado (Go 9×9)**: `rules/go/` (tablero, grupos/libertades, captura, suicidio, ko
  simple, **pase/dos pases** y **superko**), `eval/go_eval.hpp` (territorio/capturas/atari),
  `knowledge/patterns.hpp` (apertura 4-4/3-4) y `GoSearcher` (el mismo buscador genérico);
  HOST-152/153/155. Pendiente: patrones 3×3/5×5 desde disquete y MCTS ligero.
- **Implementado (variantes de ajedrez y torneos)**: Chess960 (enroque generalizado),
  King of the Hill y Three-check (`variant_score`), y `tournament.hpp`/`tools/board/arena.sh`;
  HOST-154/156. Pendiente: UI de variante en el juego y más variantes (Crazyhouse, Atomic…).
- **Implementado (conocimiento en el motor)**: sonda del libro de aperturas
  (`rules/chess/opening.hpp`) y finales teóricos en la evaluación; HOST-157.
- **Implementado (transversal)**: primitivas de concurrencia abstractas `eng::parallel`
  (`hardware_threads`, `Thread`, `Mutex`, `Atomic`, `ConditionVariable`, `StopSource`,
  `for_each_index`), no-ops en m68k y hilos reales en el host; HOST-137. Diseño en
  [PARALLEL_AND_THREADS.md](../../engine/architecture/PARALLEL_AND_THREADS.md).
- **Primitivas ya disponibles** que se reutilizan: `eng::util::lru_cache` (HOST-127, caché de
  conocimiento), `eng::util::hash`/`bitset`, `eng::util::union_find` (HOST-119, grupos de Go),
  `eng::util::priority_queue`/`hash_map`, `eng::util::arena`/`allocator` (memoria estática),
  `eng::math` (`Fixed`) y `eng::task` como cola cooperativa
  ([BACKGROUND_TASKS.md](../../engine/architecture/BACKGROUND_TASKS.md), para el pondering).
- **E/S de disco**: el contrato de carga ya existe en
  [STREAMING_LOADER.md](../../engine/architecture/STREAMING_LOADER.md) (tres estados
  `Ready`/`Empty`/`Pending` y trackloader); `storage/` se apoya en él.
- **Herramientas host** (`tools/`) y **assets** fuente/generados siguen las reglas de
  [STRUCTURE.md](../../STRUCTURE.md) §5–§6.

## 3. Reglas transversales (criterios de aceptación)

- **Sin heap**: el motor, la búsqueda y las cachés se instancian en memoria estática, con
  capacidad dimensionada por `MemoryBudget`; la pila de búsqueda (32–64 kB) no vive en la pila
  del 68000.
- **Determinismo**: sin `float` en caminos reproducibles; puntuaciones enteras y desempates
  estables. Los escenarios de test son reproducibles byte a byte.
- **Coste visible**: `MaxNodes`, tiempo y tamaño de TT/caché son parámetros explícitos; los
  límites por plataforma van en la cabecera. Pondering y análisis se ejecutan en rodajas, nunca
  como trabajo no acotado por frame.
- **Footprint medible**: cada perfil (`P20`…`P1M`) se cierra con el tamaño real de las
  estructuras (sonda `Show<sizeof(T)>`) y se registra la matriz de memoria por perfil.
- **Sin libcalls ni 68020 en caminos por nodo**: sonda de codegen (misma política que
  `eng::ai`); Zobrist y claves de 64 bits empaquetadas en `u32`, nunca `unsigned long long`.
- **Concurrencia abstracta**: la lógica usa `eng::parallel`, nunca `<thread>`. En el Amiga las
  primitivas son no-ops y `hardware_threads() == 1`; el reparto entre CPUs solo se activa con
  `hardware_threads() > 1` y no puede alterar el resultado.
- **Interfaces seguras**: sin punteros crudos ni `char*` en la frontera. Buffers como `Span`,
  texto como `StringView`, tablas como `eng::util::Array`, bloques con el concept `BlockSource`,
  serialización con `ByteReader`/`ByteWriter` y backends como políticas de plantilla (nada de
  punteros a función ni `void*`). Acumuladores e índices usan `board_int` (`eng::intw`); `s32`
  solo donde el rango lo exige.
- **No duplicar**: reutilizar `eng::util`/`eng::math`/`Loader`/`eng::parallel` antes de crear;
  una detección o utilidad que ya exista se extiende, no se copia.
- **Verificación**: `tests/host/NNN` con `README.md`, cruce `m68k`, y juego en `games/` como
  verificación de hardware; actualizar [BOARD_GAME_AI.md](../../engine/architecture/BOARD_GAME_AI.md)
  (inventario) y este roadmap en la misma pasada.
- **Referencia de optimización**: [OPTIMIZACION_GPP_68000.md](../optimization/OPTIMIZACION_GPP_68000.md)
  §12 para el comentario de optimizaciones y el port a asm de rutinas calientes.

## 4. Fases

### B0 — Núcleo y presupuesto de memoria

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B0.1 | `core/types.hpp` | `Color`/`Piece`, casilla **0x88**, `Move` (7+7+16) y `MoveList` | **HOST-138** (hecho) |
| B0.2 | `core/zobrist.hpp` | Generador xorshift32 `constexpr` y acumulador XOR; clave `u32` | **HOST-138** (hecho) |
| B0.3 | `core/game.hpp` | Concepto `GameRules` (tipos, `initial`, `generate_legal`, `in_check`, `zobrist`, `terminal`) | **HOST-138** (hecho) |
| B0.4 | `core/budget.hpp` | Perfiles `P20`…`P1M` y `plan_memory`: elige el mayor que cabe en la RAM libre | **HOST-139** (hecho) |

Cierre: tipos y presupuesto verificados; decisiones (entrada de TT 12 B, Zobrist `u32`) fijadas
en [BOARD_GAME_AI.md](../../engine/architecture/BOARD_GAME_AI.md) §9. Sonda de tamaños por perfil.

### B0.5 — Concurrencia y hilos (transversal)

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B0.5.1 | `parallel/parallel.hpp` | `hardware_threads`, `Thread`, `Mutex`/`LockGuard`, `Atomic`, `ConditionVariable`, `StopSource`/`StopToken`, `for_each_index`; no-ops en m68k | **HOST-137** (hecho) |

Cierre: la lógica puede paralelizarse en targets modernos sin romper el Amiga; diseño en
[PARALLEL_AND_THREADS.md](../../engine/architecture/PARALLEL_AND_THREADS.md). Consumidor:
búsqueda paralela en B5.

### B1 — Reglas de ajedrez

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B1.1 | `rules/chess/board.hpp` | Tablero **0x88**, Zobrist incremental, `make`/`unmake`, ataques y jaque | **HOST-140** (hecho) |
| B1.2 | `rules/chess/movegen.hpp` | Generación **legal** (pseudo-legal + filtro make/unmake) y `perft` | **HOST-140** (hecho) |
| B1.3 | `rules/chess/rules.hpp` + `history.hpp` | Jaque mate, ahogado, 50 movimientos, material insuficiente y **repetición (3×)** | **HOST-140/141** (hecho) |
| B1.4 | `rules/chess/endgame.hpp` | Sondeo de finales teóricos (KRK, KQK, KBNK, KPvK con regla del cuadrado) | **HOST-141** (hecho) |
| B1.5 | `rules/chess/notation.hpp` | FEN/EPD (`fen.hpp`) y SAN/algebraica (`notation.hpp`) | **HOST-140/142** (hecho) |

Cierre: perft vs valores conocidos (inicial/Kiwipete/al paso/promoción), repetición y finales de
referencia resueltos (HOST-140/141/142).

### B2 — Búsqueda adversaria genérica

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B2.1 | `search/search.hpp` | Negamax + Alpha-Beta + **iterative deepening**, interrumpible por nodo/tiempo (aspiration pendiente) | **HOST-143** (hecho) |
| B2.2 | `search/search.hpp` (quiescence) | Quiescence de capturas y jaques (SEE pendiente de medir) | **HOST-143** (hecho) |
| B2.3 | `rules/chess/ordering.hpp` | MVV-LVA + killer moves + history heuristic | **HOST-144** (hecho) |
| B2.4 | `search/tt.hpp` | Transposition table (entrada de 12 B), reemplazo directo | **HOST-144** (hecho) |
| B2.5 | `rules/chess/ordering.hpp` | El rol de *refutation* en perfiles bajos lo cubren killers + history (sin cabecera aparte) | **HOST-144** (hecho) |
| B2.6 | `search/pruning.hpp` | Null-move (≥ `P256`), configurable y desactivado en final/jaque | **HOST-148** (hecho) |

Cierre: mates en N y posiciones tácticas resueltos (hecho: HOST-143); la TT reduce nodos sin
corromper (hecho: HOST-144); null-move operativo (hecho: HOST-148). Aspiration y SEE quedan como
mejoras a medir.

### B3 — Evaluación de ajedrez y rasgos

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B3.1 | `eval/chess_eval.hpp` | Material + PST + movilidad + peones + seguridad del rey | **HOST-145** (hecho) |
| B3.2 | `eval/features.hpp` | `DevelopmentFeatures` (menores/mayores sin desarrollar, dama prematura, rey en el centro, enroques, torres, fase) | **HOST-145** (hecho) |
| B3.3 | `eval/chess_eval.hpp` (centro) | Control del centro vía PST de centralización | **HOST-145** (hecho) |

Cierre: la evaluación separa táctica y estrategia; los rasgos se calculan una vez y sirven a la
explicación (hecho).

### B4 — Conocimiento externo y almacenamiento

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B4.1 | `storage/block_source.hpp` | `BlockSource` con el contrato `Ready`/`Empty`/`Pending`; fuente RAM | **HOST-146** (hecho) |
| B4.2 | `knowledge/cache.hpp` | Caché LRU de bloques sobre `eng::util::lru_cache` | **HOST-146** (hecho) |
| B4.3 | `knowledge/book.hpp` | Libro de aperturas: índice por clave Zobrist → jugada + nombre | **HOST-147** (hecho) |
| B4.4 | `knowledge/endgame_tables.hpp` | Tablas de finales (nivel 3); sonda bajo demanda | **HOST-147** (hecho) |
| B4.5 | `tools/board/pack-book.sh` | Packer host texto → blob de libro (usa las reglas del engine) | **Hecho** (finales/patrones pendientes) |
| B4.6 | Fuente disco / FS PC | `FileBlockSource` (PC) hecho; trackloader del Amiga pendiente | **HOST-151** / pendiente |
| B4.7 | `rules/chess/opening.hpp` + `eval/chess_eval.hpp` | Consumo: sonda del libro por clave y finales teóricos en la evaluación | **HOST-157** (hecho) |

Cierre: contenedor de conocimiento, E/S de fichero del PC y packer de libro hechos (round-trip
struct→bloque→struct sin alineación). El backend de disquete del Amiga y los packers de
tablas/patrones quedan pendientes; se enchufan al mismo concept `BlockSource`.

### B5 — Tiempo, pondering y Multi-PV

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B5.1 | `search/time.hpp` | `TimeManager` por nodos/reloj inyectable (VBlank/CIA); nunca *busy-wait* | **HOST-148** (hecho) |
| B5.2 | `search/search.hpp` (`ponder`) | Búsqueda continua cooperativa con TT/ordenación vivos entre turnos | **HOST-148** (hecho) |
| B5.3 | `search/search.hpp` (`search_multi_pv`) | N mejores líneas con score y PV | **HOST-148** (hecho) |
| B5.4 | Reuso de árbol | TT persistente y mejor jugada previa reutilizadas entre búsquedas | **HOST-144/148** (hecho) |
| B5.5 | Búsqueda paralela | Reparto de la raíz con `eng::parallel`, determinista y secuencial en Amiga | **HOST-148** (hecho) |

Cierre: el motor "sigue pensando" sin frenar el frame y expone 2–4 líneas con su PV; el reparto
paralelo no altera el resultado (hecho). Falta el bucle de juego real (B8).

### B6 — Explicación en lenguaje natural

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B6.1 | `explain/explain.hpp` | Motor de reglas: rasgos → 2–4 frases por prioridad | **HOST-149** (hecho) |
| B6.2 | `explain/templates.hpp` | Plantillas con huecos (`{side}`, `{side_adj}`, `{diff}`) y tono (neutra/enfática) | **HOST-149** (hecho) |
| B6.3 | `knowledge/patterns.hpp` | Detección de aperturas por patrón (además del nombre del libro) | Pendiente |
| B6.4 | `tools/board/nlg-pack.ts` | Compila y **valida huecos** de las plantillas ES/EN; carga solo idioma+fase | Pendiente |
| B6.5 | Packs ES/EN | En `assets/amiga/board/*/explain/` (texto fuente) | Pendiente |

Cierre: el motor produce frases coherentes en ES y EN con tono y truncado seguro (hecho). Faltan
la detección por patrón, el packer y los assets externos.

### B7 — Go 9×9

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B7.1 | `rules/go/board.hpp` | Tablero 9×9 (81 B), grupos/libertades (`union_find` o flood-fill), ko, suicidio | **HOST-152** (hecho) |
| B7.2 | `rules/go/movegen.hpp` | Jugadas legales y conteo de prisioneros | **HOST-152** (hecho) |
| B7.3 | `eval/go_eval.hpp` | Territorio (flood-fill), capturas y ataris | **HOST-153** (hecho) |
| B7.4 | `knowledge/patterns.hpp` (Go) | Apertura/fuseki: puntos estrella (4-4) y komoku (3-4) | **HOST-155** (hecho) |
| B7.5 | `search/` (Go) + `rules/go` | `GoSearcher` (alpha-beta + TT), **pase/dos pases** y **superko** | **HOST-153/155** (hecho) |

Cierre: Go 9×9 legal y jugable en `P20`–`P64` con el mismo buscador que el ajedrez. Patrones
3×3/5×5 desde disquete y **MCTS muy ligero** (≥ 256–512 kB) quedan como líneas futuras.

Cierre: 9×9 legal y jugable en `P20`–`P64`; 13×13 solo con ≥ 512 kB; 19×19 **fuera de diseño**.

### B8 — Juegos y verificación en hardware

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B8.1 | `games/100_chess` | Ajedrez jugable (tablero + cursor + reglas + motor + explicación) | **build → run → analyze OK** (captura); pulido visual pendiente |
| B8.2 | `games/101_go` | Go 9×9 jugable (mismo flujo) | **build → run → analyze OK** (captura); pase/superko y pulido pendientes |
| B8.2b | `demos/amiga/123_chess_match` | Partida autónoma entre dos motores (estilos agresivo/posicional), juez narrador, relojes y libro en memoria | **build → run → analyze OK** (captura); verificado `1. e4`, comentario del juez y resalte de última jugada |
| B8.2c | `tools/board/selfplay` | Partidas completas en host con la misma configuración que la demo y export a **PGN** (cabeceras, SAN, comentarios y resultado) | **OK**: partidas terminadas en standard y Chess960; HOST-160 cubre PGN y libro |
| B8.3 | Matriz de rendimiento | Nodos/s y fps por CPU (68000/020/030) y perfil (`P20`…`P1M`); TT/caché vivos | Informe en `docs/debugging/` o `artifacts/` |
| B8.4 | (Opcional) 13×13 | Solo si B8.3 confirma margen en A1200 | Demo/juego y medida |

Cierre: las APIs dejan de estar "NO VERIFICADAS" y el roadmap se marca completo por fase. B8.1 y
B8.2 verificadas (build → run → analyze); B8.3 pendiente.

### B9 — Variantes de ajedrez y torneos

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B9.1 | `rules/chess/variant.hpp` | **Chess960 / Fischer Random**: las 960 disposiciones (alfiles en colores opuestos, rey entre torres) y posición inicial reproducible | **HOST-154** (hecho) |
| B9.2 | `board.hpp` + `movegen.hpp` | Enroque **generalizado** (rey/torres en columnas arbitrarias; `castle_rook` en el estado) | **HOST-154** (hecho) |
| B9.3 | `tournament.hpp` | Partida completa con presupuesto de nodos y torneo rápido con arranques de variante (`arena_chess`) | **HOST-154** (hecho) |
| B9.4 | `tools/board/arena.sh` | Herramienta de torneo (standard/chess960, semilla, nodos/jugada) | Ejecutada en host |
| B9.5 | Otras variantes | **King of the Hill**, **Three-check** con `variant_score` en el buscador | **HOST-156** (hecho) |

Cierre: el motor juega Chess960, King of the Hill y Three-check, y se enfrenta a sí mismo en
torneos rápidos.

## 5. Dependencias entre fases

```text
   B0 núcleo/budget ──► B0.5 concurrencia (eng::parallel, transversal)
        │
        ▼
   B1 reglas de ajedrez ──► B2 búsqueda ──► B3 evaluación
        │                        │              │
        │                        ▼              ▼
        │                   B4 conocimiento externo (libro/finales/patrones + storage)
        │                        │
        │                        ▼
        │                   B5 tiempo/ponder/MultiPV (+ búsqueda paralela sobre eng::parallel)
        │
        ▼
   B6 explicación NLG (usa rasgos de B3 + nombres de B4)

   B7 Go 9×9 (reutiliza B0/B0.5/B2/B4; reglas y evaluación propias)
        │
        ▼
   B8 juegos y verificación en hardware
```

B0 es cimiento y B0.5 es transversal (lo consumen la búsqueda de B5 y, si conviene, B4/B7). B1–B3
son el camino de ajedrez. B4 es transversal (lo consumen B5, B6 y B7). B6 depende de los rasgos
de B3 y de los nombres de B4. B7 puede solaparse con B4–B6 porque comparte búsqueda y storage. B8
cierra cada juego con verificación real.

## 6. Distribución de tests host

Los números son únicos y no reutilizables; el siguiente libre es **158**. Antes de crear cada
pieza se comprueba que no duplica una primitiva de `eng::util`/`eng::parallel`
([TEMPLATE_LIBRARY.md](../../engine/architecture/TEMPLATE_LIBRARY.md) y
[PARALLEL_AND_THREADS.md](../../engine/architecture/PARALLEL_AND_THREADS.md)).

| Test | Cubre | Estado |
|---|---|---|
| HOST-137 | `eng::parallel`: hilos, mutex, atómicos, condición, stop y `for_each_index` | **Hecho** |
| HOST-138 | `core/`: tipos, Zobrist y concepto `GameRules` | **Hecho** |
| HOST-139 | `core/budget.hpp`: perfiles y selección por RAM libre | **Hecho** |
| HOST-140 | Ajedrez: FEN, make/unmake, Zobrist, perft y fin de partida | **Hecho** |
| HOST-141 | Ajedrez: repetición (3×) y sondeo de finales teóricos | **Hecho** |
| HOST-142 | Notación SAN/algebraica y UCI | **Hecho** |
| HOST-143 | Búsqueda: negamax/αβ, iterative deepening, quiescence (mates en N) | **Hecho** |
| HOST-144 | Ordenación, TT y reuso (reducción de nodos, sin corrupción) | **Hecho** |
| HOST-145 | Evaluación de ajedrez y `DevelopmentFeatures` | **Hecho** |
| HOST-146 | `BlockSource` (3 estados) + caché LRU de bloques | **Hecho** |
| HOST-147 | Libro de aperturas y tablas de finales (round-trip por bloque) | **Hecho** |
| HOST-148 | `TimeManager`, ponder, null-move, PV/Multi-PV y análisis paralelo | **Hecho** |
| HOST-149 | NLG: reglas, plantillas ES/EN, tono y truncado | **Hecho** |
| HOST-150 | `binary.hpp`: cursores `ByteReader`/`ByteWriter` sobre `Span` | **Hecho** |
| HOST-151 | `FileBlockSource`: E/S real de bloques desde fichero del PC | **Hecho** |
| HOST-152 | Go: tablero, grupos/libertades, ko, suicidio y movegen | **Hecho** |
| HOST-153 | Go: evaluación de territorio/capturas y búsqueda (`GoSearcher`) | **Hecho** |
| HOST-154 | Chess960 (960 disposiciones, enroque generalizado) y torneos rápidos (`tournament.hpp`) | **Hecho** |
| HOST-155 | Go: pase/dos pases, superko y patrones de apertura | **Hecho** |
| HOST-156 | Ajedrez: King of the Hill y Three-check (`variant_score`) | **Hecho** |
| HOST-157 | Ajedrez: libro de aperturas en el motor y finales en la evaluación | **Hecho** |
| Tools | Packers host (`tools/board/`) con round-trip y validación de huecos | Pendiente |

## 7. Extensiones, decisiones tomadas y descartado

- **Decisiones fijadas** (detalle en [BOARD_GAME_AI.md](../../engine/architecture/BOARD_GAME_AI.md)
  §9): entrada de TT de 12 B; Zobrist de una palabra `u32`; tablero 0x88; `Move` 7+7+16;
  legalidad por `make`/`unmake` validada con perft; presupuesto por `planned_bytes()`; y
  concurrencia abstracta con `eng::parallel`.
- **Bitboards en ajedrez**: solo extensión futura con ≥ 512 kB; la base es 0x88/mailbox.
- **19×19 en Go**: descartado por diseño (varios MB); 13×13 solo con ≥ 512 kB.
- **Redes neuronales (NNUE)**: descartadas; el objetivo es el comportamiento clásico fuerte
  (profundidad + ordenación + TT + evaluación de rasgos).
- **Puntuación exacta de finales nivel 3**: tablas de 3–4 piezas en disquete, solo si B4 y B8
  confirman margen.
- **Presupuesto de búsqueda**: fijar `MaxNodes`/tiempo por CPU y perfil; el pondering no debe
  degradar el frame.
- **Formato de bloques de conocimiento**: decidir compresión (reutilizar el depacker del
  engine) y granularidad de bloque para equilibrar *seek* y RAM.
- **NLG**: fijar el conjunto mínimo de rasgos/plantillas de `P20` y la validación de contenido
  (que las frases no se contradigan con la evaluación real).

## 8. Cómo se cierra cada paso

1. Cabecera en `engine/include/eng/board/<área>/` con comentario didáctico (intención, coste,
   límites por plataforma y ejemplo de uso).
2. `tests/host/NNN` + `README.md` y registro en el catálogo de tests.
3. Sonda de codegen para los caminos por nodo (sin libcalls ni instrucciones 68020).
4. Actualizar [BOARD_GAME_AI.md](../../engine/architecture/BOARD_GAME_AI.md) (inventario/estado)
   y este roadmap (fase hecha) en la misma pasada.
5. Commit atómico por paso, con la referencia de la técnica y la del test.
