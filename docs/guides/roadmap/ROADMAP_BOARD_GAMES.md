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

- **No existe** ningún motor de tablero en el repositorio (ni ajedrez ni Go).
- **Primitivas ya disponibles** que se reutilizan: `eng::util::lru_cache` (HOST-127, caché de
  conocimiento), `eng::util::hash`/`bitset` (Zobrist, material), `eng::util::union_find`
  (HOST-119, grupos de Go), `eng::util::priority_queue`/`hash_map`, `eng::util::arena`/
  `allocator` (memoria estática), `eng::math` (`Fixed`) y `eng::task` como cola cooperativa
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
- **No duplicar**: reutilizar `eng::util`/`eng::math`/`Loader` antes de crear; una detección o
  utilidad que ya exista se extiende, no se copia.
- **Verificación**: `tests/host/NNN` con `README.md`, cruce `m68k`, y juego en `games/` como
  verificación de hardware; actualizar [BOARD_GAME_AI.md](../../engine/architecture/BOARD_GAME_AI.md)
  (inventario) y este roadmap en la misma pasada.
- **Referencia de optimización**: [OPTIMIZACION_GPP_68000.md](../optimization/OPTIMIZACION_GPP_68000.md)
  §12 para el comentario de optimizaciones y el port a asm de rutinas calientes.

## 4. Fases

### B0 — Núcleo y presupuesto de memoria

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B0.1 | `core/types.hpp` | `Side`, `Square`, `Piece`, `Score` (centipeones/puntos), `Move`, `MoveList<N>` | **HOST-137** |
| B0.2 | `core/zobrist.hpp` | Claves Zobrist deterministas en `u32` (o dos `u32`); sin `long long` | **HOST-137** |
| B0.3 | `core/game.hpp` | Concepto `GameRules` que especializa la búsqueda (make/unmake, legalidad, terminal, rasgos) | **HOST-137** |
| B0.4 | `core/budget.hpp` | `MemoryProfile` (`P20`…`P1M`) y `MemoryBudget`: dimensiona TT, caché y libro desde la RAM libre | **HOST-138** |

Cierre: tipos y presupuesto verificados; sonda de tamaños por perfil.

### B1 — Reglas de ajedrez

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B1.1 | `rules/chess/board.hpp` | Tablero **0x88**/mailbox, make/unmake, historial de claves | **HOST-139** |
| B1.2 | `rules/chess/movegen.hpp` | Generación **legal**: piezas, enroque, al paso, promoción, clavadas | **HOST-139** |
| B1.3 | `rules/chess/draw.hpp` | Jaque mate, ahogado, 50 movimientos, repetición (3), material insuficiente | **HOST-140** |
| B1.4 | `rules/chess/endgame.hpp` | Sondeo de finales teóricos (K vs K, KRK, KQK, KBNK, KPK con casilla del peón/oposición) | **HOST-140** |
| B1.5 | `rules/chess/notation.hpp` | FEN/EPD y algebraica/SAN (para libro, tablas y explicación) | **HOST-141** |

Cierre: perft corto vs valores conocidos; posiciones de mate/ahogado/repetición y finales de
referencia resueltos.

### B2 — Búsqueda adversaria genérica

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B2.1 | `search/search.hpp` | Negamax + Alpha-Beta + **iterative deepening** + aspiration; interrumpible por nodo/tiempo | **HOST-142** |
| B2.2 | `search/search.hpp` (quiescence) | Quiescence de capturas y jaques + SEE básico (táctica) | **HOST-142** |
| B2.3 | `search/ordering.hpp` | MVV-LVA + killer moves + history heuristic | **HOST-143** |
| B2.4 | `search/tt.hpp` | Transposition table por perfil (clave+depth+score+move+flags), *always replace* / *depth-preferred* | **HOST-143** |
| B2.5 | `search/refutation.hpp` | *Refutation table* triangular (sustituye a la TT en `P20`) | **HOST-143** |
| B2.6 | `search/pruning.hpp` | Null-move (≥ `P256`) y podas opcionales tras medir | **HOST-142/143** |

Cierre: mates en N y posiciones tácticas resueltos; la TT reduce nodos sin corromper; el perfil
`P20` funciona sin TT.

### B3 — Evaluación de ajedrez y rasgos

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B3.1 | `eval/chess_eval.hpp` | Material + PST + movilidad simple + peones + seguridad del rey | **HOST-144** |
| B3.2 | `eval/features.hpp` | `DevelopmentFeatures` (menores/mayores sin desarrollar, dama prematura, rey en el centro, enroques, torres, fase) | **HOST-144** |
| B3.3 | `eval/chess_eval.hpp` (centro) | Control del centro y coordinación mínima | **HOST-144** |

Cierre: la evaluación separa táctica y estrategia; los rasgos se calculan una vez y sirven a la
explicación.

### B4 — Conocimiento externo y almacenamiento

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B4.1 | `storage/block_source.hpp` | `BlockSource` con el contrato `Ready`/`Empty`/`Pending`; fuente RAM | **HOST-145** |
| B4.2 | `knowledge/cache.hpp` | Caché LRU de bloques sobre `eng::util::lru_cache` | **HOST-145** |
| B4.3 | `knowledge/book.hpp` | Libro de aperturas: índice por prefijo Zobrist → bloque; devuelve jugada + nombre | **HOST-146** |
| B4.4 | `knowledge/endgame_tables.hpp` | Tablas de finales en disco (nivel 3); sonda bajo demanda | **HOST-146** |
| B4.5 | `tools/board/*-pack.ts` | Packers host texto → bloques + índice (libro, finales, patrones) con round-trip | Tools + informe |
| B4.6 | Fuente disco | Adaptador de `BlockSource` al trackloader/HD del engine | Prueba de pista contra ADF |

Cierre: con el libro en disquete la RAM no crece; round-trip texto→bloque→lectura al 100 %; la
caché acota el número de *seeks*.

### B5 — Tiempo, pondering y Multi-PV

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B5.1 | `search/time.hpp` | `TimeManager` por nodos/reloj (VBlank/CIA); nunca *busy-wait* | **HOST-147** |
| B5.2 | `search/ponder.hpp` | Búsqueda continua cooperativa (rodajas en el bucle) con TT/killers vivos entre turnos | **HOST-147** |
| B5.3 | `search/ponder.hpp` (MultiPV) | N mejores líneas con score y PV; separación táctica/estratégica | **HOST-147** |
| B5.4 | Reuso de árbol | Reaprovechar TT/PV si el rival juega la jugada asumida; abortar y reanudar si no | **HOST-147** |

Cierre: el motor "sigue pensando" sin frenar el frame; con `P128+` muestra 2–4 líneas y su
puntuación.

### B6 — Explicación en lenguaje natural

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B6.1 | `explain/explain.hpp` | Motor de reglas: rasgos → 2–4 frases por prioridad, modo breve/detallado | **HOST-148** |
| B6.2 | `explain/templates.hpp` | Plantillas con huecos (`{side}`, `{piece}`, `{square}`, `{diff}`) y tono (neutra/enfática/apasionada) | **HOST-148** |
| B6.3 | `knowledge/patterns.hpp` | Detección de aperturas por patrón (además del nombre del libro) | **HOST-148** |
| B6.4 | `tools/board/nlg-pack.ts` | Compila y **valida huecos** de las plantillas ES/EN; carga solo idioma+fase | Tools + test |
| B6.5 | Packs ES/EN | En `assets/amiga/board/*/explain/` (texto fuente) | Revisión de contenido |

Cierre: dada una posición, el motor produce un párrafo coherente en ES y EN, con tono acorde a
la magnitud, en 8–50 kB según perfil.

### B7 — Go 9×9

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B7.1 | `rules/go/board.hpp` | Tablero 9×9 (81 B), grupos/libertades (`union_find` o flood-fill), ko, suicidio | **HOST-149** |
| B7.2 | `rules/go/movegen.hpp` | Jugadas legales y conteo de prisioneros | **HOST-149** |
| B7.3 | `eval/go_eval.hpp` | Territorio (flood-fill/influencia), ataris, ojos y grupos débiles | **HOST-150** |
| B7.4 | `knowledge/patterns.hpp` (Go) | Patrones 3×3/5×5 y fuseki desde disquete | **HOST-150** |
| B7.5 | `search/` (Go) | Alpha-Beta + patrones; **MCTS muy ligero** opcional con ≥ 256–512 kB | **HOST-150** |

Cierre: 9×9 legal y jugable en `P20`–`P64`; 13×13 solo con ≥ 512 kB; 19×19 **fuera de diseño**.

### B8 — Juegos y verificación en hardware

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| B8.1 | `games/100_chess` | Ajedrez jugable sobre el engine: tablero (tiles/superficie), entrada, estado y análisis en pantalla | build → run → analyze + capturas |
| B8.2 | `games/101_go` | Go 9×9 jugable (mismo flujo) | build → run → analyze |
| B8.3 | Matriz de rendimiento | Nodos/s y fps por CPU (68000/020/030) y perfil (`P20`…`P1M`); TT/caché vivos | Informe en `docs/debugging/` o `artifacts/` |
| B8.4 | (Opcional) 13×13 | Solo si B8.3 confirma margen en A1200 | Demo/juego y medida |

Cierre: las APIs dejan de estar "NO VERIFICADAS" y el roadmap se marca completo por fase.

## 5. Dependencias entre fases

```text
   B0 núcleo/budget
        │
        ▼
   B1 reglas de ajedrez ──► B2 búsqueda ──► B3 evaluación
        │                        │              │
        │                        ▼              ▼
        │                   B4 conocimiento externo (libro/finales/patrones + storage)
        │                        │
        │                        ▼
        │                   B5 tiempo/ponder/MultiPV
        │
        ▼
   B6 explicación NLG (usa rasgos de B3 + nombres de B4)

   B7 Go 9×9 (reutiliza B0/B2/B4; reglas y evaluación propias)
        │
        ▼
   B8 juegos y verificación en hardware
```

B0 es cimiento. B1–B3 son el camino de ajedrez. B4 es transversal (lo consumen B5, B6 y B7).
B6 depende de los rasgos de B3 y de los nombres de B4. B7 puede solaparse con B4–B6 porque
comparte búsqueda y storage. B8 cierra cada juego con verificación real.

## 6. Distribución de tests host planificada

Los números son **indicativos** y se asignan al implementar (son únicos y no reutilizables; el
siguiente libre es **137**). Antes de crear cada pieza se comprueba que no duplica una primitiva
de `eng::util` ([TEMPLATE_LIBRARY.md](../../engine/architecture/TEMPLATE_LIBRARY.md)).

| Test | Cubre |
|---|---|
| HOST-137 | `core/`: tipos, Zobrist, concepto `GameRules` |
| HOST-138 | `core/budget.hpp`: dimensionado por perfil y footprint medido |
| HOST-139 | Ajedrez: tablero, make/unmake y generación legal (perft corto) |
| HOST-140 | Ajedrez: reglas de tablas y sondeo de finales teóricos |
| HOST-141 | Notación FEN/EPD y SAN |
| HOST-142 | Búsqueda: negamax/αβ, iterative deepening, quiescence (mates en N) |
| HOST-143 | Ordenación, TT y refutation table (reducción de nodos, sin corrupción) |
| HOST-144 | Evaluación de ajedrez y `DevelopmentFeatures` |
| HOST-145 | `BlockSource` + LRU (contrato de tres estados y aciertos de caché) |
| HOST-146 | Libro de aperturas y tablas de finales (índice → bloque, round-trip) |
| HOST-147 | `TimeManager`, pondering, MultiPV y reuso de árbol |
| HOST-148 | NLG: selección de reglas, plantillas ES/EN y tono |
| HOST-149 | Go: tablero, grupos/libertades, ko, suicidio y movegen |
| HOST-150 | Go: evaluación de territorio/patrones y búsqueda |
| Tools | Packers host (`tools/board/`) con round-trip y validación de huecos |

## 7. Extensiones, decisiones abiertas y descartado

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
