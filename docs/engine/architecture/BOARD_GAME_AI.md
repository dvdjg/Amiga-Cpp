# Motores de juegos de tablero (`eng::board`)

`engine/include/eng/board/` reúne los motores de **juegos de tablero por turnos** que el
engine puede ejecutar en un A500–A1200: ajedrez y Go 9×9 como primeros casos, con un
**footprint de memoria entre 20 kB y 1 MB** y con **almacenamiento externo** (disquete o HD)
usado como memoria de conocimiento para no inflar la RAM. Incluye búsqueda adversaria
clásica, reglas legales y de tablas, evaluación por rasgos, libro de aperturas y tablas de
finales, y un generador de explicaciones en lenguaje natural (español e inglés) por
plantillas.

Esta familia es **distinta de `eng::ai`**: [GAME_AI_LIBRARY.md](GAME_AI_LIBRARY.md) cubre IA
de videojuego en tiempo real (planificación GOAP, decisión, navegación, steering,
percepción); `eng::board` cubre **búsqueda adversarial sobre información perfecta** y todo su
soporte (reglas, conocimiento, explicación). Ambas comparten las primitivas de `eng::util` y
`eng::math` y no las duplican.

Plan de crecimiento y catálogo de fases: [ROADMAP_BOARD_GAMES.md](../../guides/roadmap/ROADMAP_BOARD_GAMES.md).
Estructura del repositorio: [STRUCTURE.md](../../STRUCTURE.md) §3.

## 1. Capas y estructura

La familia se organiza por **responsabilidad**, con el núcleo abajo y los consumidores arriba.
El motor de búsqueda es **genérico** (no conoce ajedrez ni Go): las reglas de cada juego se
inyectan como una *policy* (`GameRules` en `core/game.hpp`), igual que el escalar es un
parámetro de plantilla en `eng::math`. Añadir un juego nuevo no reordena nada.

```text
engine/include/eng/board/
├── core/          → tipos (Side, Square, Score, Move, MoveList<N>), Zobrist,
│                     presupuesto de memoria y concepto GameRules
├── search/        → búsqueda adversaria: negamax/αβ, iterative deepening,
│                     quiescence, ordenación, TT, poda, tiempo, ponder y MultiPV
├── rules/
│   ├── chess/     → tablero 0x88, generación legal, notación, reglas de tablas,
│   │                 sondeo de finales teóricos
│   └── go/        → tablero 9×9, grupos/libertades, ko, suicidio, territorio
├── eval/          → evaluación de ajedrez y Go + extracción de rasgos compartida
├── knowledge/     → libro de aperturas, tablas de finales, patrones, caché LRU
├── storage/       → fuente de bloques externa (RAM/disquete) sobre el Loader del engine
└── explain/       → NLG por plantillas: rasgos → reglas → frases ES/EN
```

```text
   explain/   ┌──────────────────────────────────────────────────────┐
              │ rasgos → reglas de prioridad → plantillas → párrafo  │
              └──────────────────────────────────────────────────────┘
                        ▲
   eval/      ┌─────────┴───────────────────────────────────────────┐
              │ material · PST · movilidad · peones · rey · territorio│
              └─────────┬───────────────────────────────────────────┘
   search/    ┌─────────┴───────────┐        knowledge/  ┌───────────┐
              │ negamax/αβ · ID ·   │◄───────probe───────│ libro     │
              │ quiescence · TT ·   │                    │ tablas    │
              │ ordering · ponder   │                    │ patrones  │
              └─────────┬───────────┘                    └─────┬─────┘
                        │                                     │
   rules/     ┌─────────┴───────────┐                   storage/  ┌───────────┐
              │ chess/  ·  go/      │                   BlockSource│ RAM/disk  │
              │ legalidad y tablas  │                   + LRU cache│ (Loader)  │
              └─────────┬───────────┘                              └───────────┘
   core/      ┌─────────┴───────────────────────────────────────────┐
              │ Score · Move · Zobrist · MemoryBudget · GameRules    │
              └─────────────────────────────────────────────────────┘
   eng::util / eng::math / eng::task  (LRU, hash, bitset, arena, fixed, tareas)
```

Reglas de capa (las de [CODING_STYLE.md](CODING_STYLE.md)): `eng::board` no conoce hardware,
no incluye registros ni DMA y compila igual en host que en el cruce `m68k`. El acceso a disco
se hace a través de `storage/`, que se apoya en la abstracción de carga ya existente, no en
registros de Paula.

## 2. Presupuesto de memoria (20 kB – 1 MB)

El footprint total es la suma de motor + tablero + estructuras de búsqueda + conocimiento en
RAM + caché externa. El perfil se elige en `init` según la RAM libre real y se materializa en
un `MemoryBudget` (`core/budget.hpp`); ningún tamaño se fija con macros.

| Perfil | Footprint total | Transposition Table | Ordenación / poda | Libro en RAM | Caché externa | Multi-PV |
|---|---|---|---|---|---|---|
| `P20` | ~20–24 kB | ninguna (solo *refutation table*) | MVV-LVA + killers | 2–8 kB | 4 kB | 1 |
| `P64` | ~64 kB | 2–4 k entradas × 12 B | + history | 8 kB | 4–8 kB | 1 |
| `P128` | ~128 kB | 8 k × 12 B | + history | 8–16 kB | 8–16 kB | 2 |
| `P256` | ~256 kB | 16 k × 12 B | + null-move | 16 kB | 16–32 kB | 2–4 |
| `P512` | ~512 kB | 32 k × 12 B | + history de peones | 16–32 kB | 32 kB | 2–4 |
| `P1M` | ~1 MB | 64 k × 12–16 B | completa | 32–64 kB | 32–64 kB | 4 |

Notas:

- La **entrada de TT** se empaqueta agresivamente (≈12 B: clave parcial `u32`, `s16` de
  puntuación, `u8` de profundidad, `u8` de mejor jugada + flags). `budget.hpp` calcula el
  número de entradas a partir de los bytes disponibles, no al revés.
- En el modo más bajo (`P20`) **no hay TT**: se sustituye por una *refutation table*
  triangular (pequeña y barata) más killers, priorizando profundidad sobre hashing.
- La detección de RAM libre es **backend-agnóstica**: el motor consulta el modelo de memoria
  del engine ([MEMORY_MODEL.md](MEMORY_MODEL.md)) o un contador de arena; en Amiga, la
  cantidad disponible se obtiene antes del takeover (política en
  [HARDWARE_AND_ROM_KERNEL_POLICY.md](HARDWARE_AND_ROM_KERNEL_POLICY.md)).
- En 68000 se evitan divisiones y aritmética de 64 bits nativa. Zobrist se empaqueta en
  `u32` (o dos `u32` cuando se quiere verificación), siguiendo el patrón ya usado en GOAP para
  no arrastrar libcalls de `long long`.

## 3. Almacenamiento externo como memoria de conocimiento

El conocimiento (libro de aperturas, tablas de finales, patrones de Go, plantillas de
explicación) vive mayoritariamente **fuera de la RAM**. En RAM quedan solo un índice mínimo y
una **caché LRU** de los últimos bloques usados.

```text
   conocimiento fuente (texto)          tools host                out/assets/board/
   assets/amiga/board/…        ──────►  packers      ──────►      bloques + índice
                                                                       │
                                          storage::BlockSource ◄────────┘
                                          (RAM: incbin/preload
                                           disco: trackloader/HD)
                                                  │
                                          knowledge/  +  lru_cache (HOST-127)
```

- **Contrato de bloques.** `storage/block_source.hpp` expone una lectura por bloque con el
  **contrato de tres estados** ya definido por el streaming del engine
  (`Ready` / `Empty` / `Pending`, ver [STREAMING_LOADER.md](STREAMING_LOADER.md) §1). Un
  `BlockSource` enumera bloques por identificador; el índice de conocimiento mapea
  `posición/clave → bloque`.
- **Fuentes.** La fuente **RAM** (bloque incbinado o precargado al inicio) valida todo el
  camino sin disco. La fuente **disco** reutiliza el trackloader de hardware o el worker de HD
  descritos en [STREAMING_LOADER.md](STREAMING_LOADER.md) §3–§5; la base de hardware está en
  [trackloading.md](../../reference/amiga/techniques/trackloading.md). `eng::board` no
  reimplementa E/S: solo consume el mismo contrato de carga.
- **Caché LRU.** `eng::util::lru_cache` (HOST-127) guarda los últimos N bloques (4–64 kB según
  perfil). Los aciertos evitan el *seek* y la latencia mecánica (~200 ms por revolución más
  *seek*), que es el coste dominante.
- **Carga bajo demanda.** El motor pide el conocimiento **cuando lo necesita**: la sonda del
  libro se hace por prefijo de clave Zobrist; la de finales, por firma de material y posición;
  la de patrones, por el *hash* local del recorte. Nada se carga "por si acaso".

### 3.1 Contenido y ubicación canónica

El conocimiento fuente es **texto versionado**; los bloques compilados son **generados**
(ignorados por git) y se producen con las tools host.

```text
assets/amiga/board/
├── chess/
│   ├── book/          → líneas de aperturas (fuente PGN/EPD simplificado)
│   ├── endgames/      → tablas de finales en texto
│   ├── patterns/      → reglas de detección de aperturas (declarativas)
│   └── explain/       → plantillas ES/EN (ES, EN) por categoría y tono
└── go/
    ├── patterns/      → patrones 3×3 / 5×5 y fuseki
    └── explain/       → plantillas ES/EN de Go

out/assets/board/<pipeline>/   → bloques + índice generados (gitignored)
```

Las tools de empaquetado viven en `tools/board/` (build host del pipeline, con `--out` de
defecto canónico) y sus salidas siguen las reglas de [STRUCTURE.md](../../STRUCTURE.md) §6.1.

## 4. Búsqueda adversaria (`search/`)

El motor de búsqueda es **el mismo** para ajedrez y Go; solo cambia la *policy* de reglas y la
evaluación. Técnicas, todas clásicas y aptas para 68000:

| Técnica | Efecto | Footprint |
|---|---|---|
| Negamax + Alpha-Beta | búsqueda básica con poda | nulo |
| Iterative deepening + aspiration windows | refina en cualquier momento y permite interrumpir | nulo |
| Quiescence search (capturas y jaques) | evita el efecto horizonte | bajo |
| Ordenación: MVV-LVA, killers, history | encuentra antes las líneas críticas | bajo |
| Null-move pruning | más profundidad cuando hay recursos (≥ `P256`) | bajo |
| Transposition table | reutiliza trabajo y sostiene el *pondering* | según perfil |
| Refutation table | alternativa barata a la TT en `P20` | muy bajo |
| Time manager (nodos/reloj) | nunca *busy-wait*; interrupción limpia | nulo |
| Pondering / búsqueda continua | piensa con el reloj del rival | usa TT/killers history |
| Multi-PV | N mejores líneas con su puntuación | TT + lista de N |

Cuestiones de diseño en 68000:

- **Iterative deepening** permite **interrumpir** la búsqueda en cualquier nodo (el reloj es
  lento); la mejor línea parcial queda disponible y la TT/killers sobreviven al corte.
- **Pondering** se ejecuta de forma **cooperativa** en el bucle del engine (por rodajas, con
  la cola de [BACKGROUND_TASKS.md](BACKGROUND_TASKS.md)), cediendo el turno al render; no hay
  segundo hilo ni `busy-wait`. El motor **nunca está parado**: sigue refinando mientras se
  espera al rival o a la entrada.
- **TT persistente entre turnos.** Cuando el rival juega la jugada asumida, se reaprovecha el
  árbol vía TT/PV; si juega otra, se aborta y se reinicia desde la nueva posición
  conservando la TT.
- La **separación táctica/estratégica** de la puntuación se obtiene de la propia búsqueda:
  la táctica la fija quiescence + SEE (intercambio estático); la estrategia, la evaluación de
  rasgos. Con memoria suficiente el Multi-PV expone ambas por candidata.
- Sin `float`: las puntuaciones son enteras (centipeones en ajedrez, puntos en Go). Los
  desempates son estables.

## 5. Reglas de tablas y finales teóricos

Todo motor legal debe conocer las reglas de tablas. Ocupan unos pocos kB.

| Regla | Detección | Coste |
|---|---|---|
| Jaque mate / rey ahogado | sin jugadas legales y rey en jaque o no | casi nulo (ya se genera la lista) |
| Repetición (3 veces) | misma posición (turno, enroques, al paso) 3 veces; historial de claves Zobrist | historial acotado (~2 kB) |
| Regla de los 50 movimientos | contador de medios movimientos sin captura ni peón | `u8` |
| Material insuficiente | K vs K, K+N vs K, K+B vs K, K+B vs K+B mismo color… | < 200 B |
| Finales teóricos | sonda por firma de material y posición (KRK, KQK, KBNK, KPK…) | ~1 kB de lógica |

- El historial de repetición se acota a la ventana relevante (los últimos 100–150 hashes
  bastan en la práctica en `P20`); en perfiles altos cabe la partida completa.
- `KPK` (rey y peón contra rey) se resuelve con la **regla de la casilla del peón** y la
  oposición, sin tablas externas.
- El motor **busca tablas de forma natural**: al maximizar su puntuación, cuando está peor la
  mejor línea tiende a 0.00; reconocer finales de tablas y liquidar hacia ellos refuerza ese
  comportamiento sin un "modo tablas" especial.

## 6. Evaluación por rasgos (`eval/`)

La evaluación es **ligera** pero con componentes táctica y estratégica. Los rasgos se calculan
una vez por posición y se comparten entre la evaluación y el explicador de lenguaje natural
(una sola fuente de verdad).

Rasgos de ajedrez: material, tablas pieza-casilla (PST), movilidad simple, estructura de
peones (doblados/aislados/retrasados), seguridad del rey, control del centro y **rasgos de
desarrollo**.

Rasgos de Go: territorio estimado (flood-fill o mapa de influencia), capturas y ataris,
grupos con pocos ojos y patrones locales 3×3 / 5×5 precalculados.

## 7. Explicación en lenguaje natural (`explain/`)

Generación por **plantillas + reglas** (template-based NLG), sin ningún modelo de lenguaje: se
seleccionan y combinan frases preescritas según los rasgos. Es de altísimo valor percibido con
coste bajo (8–50 kB según riqueza).

```text
   eval::features (una vez por posición)
        │
        ▼
   reglas de prioridad  → eligen 2–4 rasgos más relevantes
        │
        ▼
   plantillas por idioma y tono (ES/EN; neutra/enfática/apasionada)
        │
        ▼
   composición con conectores ("Además," / "Por otro lado," / "Sin embargo,")
        │
        ▼
   frase breve (1 frase) o párrafo (2–4 frases)
```

- **Estructura de rasgos** (≈12–16 B), compartida con la evaluación: piezas menores/mayores sin
  desarrollar por bando, dama movida prematuramente, rey en el centro, enroques disponibles,
  torres en columnas abiertas y fase de la partida.
- **Detección de aperturas** por dos vías: el libro (nombre exacto, incluido ECO) y reglas de
  patrón de peones/piezas cuando la posición sale del libro (Gambito de Dama, Española,
  Siciliana, Francesa, Caro-Kann, Inglesa, London…). La tabla de patrones ocupa 1–3 kB y puede
  residir en disquete.
- **Tono**: los umbrales de magnitud (amenaza de mate, gran desequilibrio, debilidad grave)
  seleccionan la variante enfática o apasionada.
- **Coste**: lógica 4–8 kB; plantillas 8–12 kB (mínimo), 20–30 kB (bueno) o 35–50 kB (rico),
  cargables por idioma y fase desde `assets/`.

## 8. Representación por juego

### 8.1 Ajedrez

- Tablero **0x88** o *mailbox* 12×10/16×12: la generación de movimientos es más simple y rápida
  que bitboards puros en un 68000 sin aritmética de 64 bits.
- Bitboards solo tienen sentido con ≥ 512 kB y optimización dedicada; quedan como extensión
  futura, no como base.
- Pila de búsqueda de 32–64 kB (la búsqueda es recursiva), instanciada en memoria estática.
- Notación FEN/EPD y algebraica/SAN para el libro, las tablas y los comentarios.

### 8.2 Go 9×9

- Tablero de 81 bytes; grupos y libertades con unión-búsqueda
  (`eng::util::union_find`, HOST-119) o *flood-fill*.
- Ko por historial de claves Zobrist; suicidio prohibido.
- **9×9** es el tamaño objetivo en el rango de 20 kB–1 MB. **13×13** solo con ≥ 512 kB; **19×19**
  queda fuera del rango (varios MB) y se documenta como no soportado por diseño.
- Búsqueda: Alpha-Beta + evaluación de territorio/patrones (profundidad baja con búsqueda
  selectiva) cuando hay poca memoria; **MCTS muy ligero** (pocas simulaciones, expansión
  limitada) solo si hay ≥ 256–512 kB. Los patrones de fuseki y locales se sirven desde
  disquete.

## 9. Verificación

- **Test host** por pieza (búsqueda, reglas, tablas, TT, caché, NLG) en `tests/host/NNN` con su
  `README.md`; los escenarios son deterministas (mates en N, posiciones de libro, secuencias
  de repetición, patrones conocidos).
- **Cruce `m68k`** con sonda de codegen para los caminos por nodo (sin libcalls de libgcc ni
  instrucciones 68020) y medida de tamaños con `Show<sizeof(T)>`; presupuesto acotado por
  `MaxNodes`/tiempo.
- **Juego/demo** en `games/` como verificación final de hardware (regla de `AGENTS.md`: toda
  API sin demo queda **NO VERIFICADA**). Detalle en [docs/testing/README.md](../../testing/README.md).
- El pipeline de conocimiento (packers host) se verifica con round-trip texto → bloque →
  lectura, igual que el pipeline de tiles.

## 10. Inventario

| Área | Estado |
|---|---|
| `core/` (tipos, Zobrist, budget, concepto `GameRules`) | Planificado (B0) |
| `rules/chess/` (tablero, legalidad, notación, tablas, finales) | Planificado (B1) |
| `search/` (negamax/αβ, ID, quiescence, TT, ordering, ponder, MultiPV) | Planificado (B2, B5) |
| `eval/` (ajedrez y Go, rasgos) | Planificado (B3, B7) |
| `knowledge/` + `storage/` (libro, tablas, patrones, BlockSource, LRU) | Planificado (B4) |
| `explain/` (NLG ES/EN por plantillas) | Planificado (B6) |
| `rules/go/` + `eval/go` | Planificado (B7) |
| Juegos en `games/` | Planificado (B8) |

> Estado: **planificado**. El plan por fases, la distribución de tests y los criterios de
> cierre están en [ROADMAP_BOARD_GAMES.md](../../guides/roadmap/ROADMAP_BOARD_GAMES.md), fuente
> única del avance. Este documento describe el diseño vigente y no se duplica allí.
