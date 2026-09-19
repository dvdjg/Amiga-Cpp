# Motores de juegos de naipes (`eng::cards`)

`engine/include/eng/cards/` reúne los motores de **juegos de naipes con información
imperfecta** que el engine puede ejecutar en un A500–A1200. El primer caso es el **póker
Texas Hold'em No-Limit** (2..10 jugadores), con un **footprint entre 20 kB y 512 kB**.
A diferencia de los juegos de tablero, aquí no hay búsqueda adversaria sobre información
perfecta: hay **azar** (reparto), **información oculta** (cartas de los rivales) y
**apuestas**; el motor decide por *equity* Monte Carlo, pot odds y un modelo ligero de
rival.

Esta familia es **distinta de `eng::board`** ([BOARD_GAME_AI.md](BOARD_GAME_AI.md)) y de
`eng::ai` ([GAME_AI_LIBRARY.md](GAME_AI_LIBRARY.md)): `eng::board` cubre búsqueda
adversaria con información perfecta; `eng::ai` cubre IA en tiempo real; `eng::cards` cubre
**juegos con azar, información oculta y rondas de apuestas**. Las tres comparten las
primitivas de `eng::util`, `eng::math` y el PRNG `eng::Xoroshiro64pp`, y no las duplican.

Plan de crecimiento y catálogo de fases: [ROADMAP_CARD_GAMES.md](../../guides/roadmap/ROADMAP_CARD_GAMES.md).
Estructura del repositorio: [STRUCTURE.md](../../STRUCTURE.md) §3.

## 1. Capas y estructura

La familia se organiza por **responsabilidad**, con el núcleo abajo y los consumidores
arriba. El estado de la mesa y las reglas son independientes de la política de decisión:
un *bot* consume las acciones legales, no las inventa.

```text
engine/include/eng/cards/
├── core/          → tipos (Suit, Rank, Card, Hand, Street), Deck, presupuesto (N20…N512)
│                     y aritmética sin libcalls (intmath: divu.w / mulu16)
├── rules/
│   ├── hand_rank.hpp     → evaluador de 5/7 cartas (mejor de 5 entre 7)
│   └── texas_holdem.hpp  → estado de mesa, ciegas, acciones legales, side pots y showdown
├── eval/
│   ├── equity.hpp        → equity Monte Carlo, pot odds y heurística preflop
│   └── range.hpp         → 169 clases de mano inicial, rangos y tabla preflop
├── ai/
│   └── bot.hpp           → política (equity + pot odds + estilo) y modelo de rival
└── sim/
    └── session.hpp       → partidas CPU vs CPU para ajustar el nivel desde host
```

```text
   sim/       ┌──────────────────────────────────────────────────────────┐
              │ run_session: N manos, botón rota, net y bb/100            │
              └───────────────────────────┬──────────────────────────────┘
                                          │
   ai/        ┌───────────────────────────┴──────────────────────────────┐
              │ decide: equity vs pot odds · estilo · modelo de rival     │
              └───────────────────────────┬──────────────────────────────┘
                    equity │                          │ acciones legales
   eval/      ┌────────────┴────────────┐   rules/  ┌─┴──────────────────┐
              │ Monte Carlo · pot odds  │◄──────────│ texas_holdem        │
              │ heurística preflop      │           │ hand_rank           │
              └────────────┬────────────┘           └─┬──────────────────┘
                           │                          │
   core/      ┌────────────┴──────────────────────────┴──────────────────┐
              │ Card · Deck · Street · HandValue · CardPlan               │
              └───────────────────────────┬──────────────────────────────┘
                                          │
   eng::util / eng::math / eng::Xoroshiro64pp / eng::parallel (reutilizados)
```

Reglas de capa (las de [CODING_STYLE.md](CODING_STYLE.md)): `eng::cards` no conoce
hardware, no incluye registros ni DMA y compila igual en host que en el cruce `m68k`. El
azar entra **solo** por un `eng::Xoroshiro64pp` inyectado: una semilla reproduce la
partida completa. El reparto entre CPUs, cuando el target las tiene, usa `eng::parallel`;
en el Amiga degrada a ejecución secuencial.

## 2. Presupuesto de memoria (20 kB – 512 kB)

El footprint total es la suma de motor + estado de mesa + equity + modelo de rival +
histórico + explicación. El perfil se elige en `init` según la RAM libre real y se
materializa en un `CardPlan` (`core/budget.hpp`); ningún tamaño se fija con macros.

| Perfil | Footprint | Muestras MC / jugada | Modelo de rival | Histórico | Explicación |
|---|---|---|---|---|---|
| `N20` | ~17–20 kB | 0 (heurística preflop) | — | 16 acciones | mínima |
| `N64` | ~64 kB | 8 | 1 rival | 64 acciones | básica |
| `N128` | ~128 kB | 16 | 3 rivales | 128 acciones | media |
| `N256` | ~256 kB | 32 | 6 rivales | 256 acciones | rica |
| `N512` | ~512 kB | 64 | 8 rivales | 512 acciones | completa |

Las muestras están **calibradas en un A500 real** con `demos/amiga/124_cards_bench`
(unidad = 1 mano + 1 muestra, cronometrada por TOD a 50 Hz): `N20` ≈ 30 unidades/s
(33 ms), `N64` ≈ 1 unidad/s (641 ms), `N128` ≈ 2,75 s, `N256` ≈ 5,09 s y `N512` ≈ 44,6 s
por unidad. `N20`/`N64` son los perfiles viables en un A500 base; `N128` y superiores
apuntan a máquinas ampliadas (A1200/030).

Notas:

- Los **tamaños reales en m68k** están fijados en la sonda de codegen
  (`tools/analyze/codegen-report.mjs`): `Seat` 18 B, `Table` 270 B, `Deck` 53 B,
  `CardPlan` 34 B, `HandRange` 24 B, `PreflopTable` 512 B, `EquityResult` 6 B,
  `BotParams` 14 B, `OpponentModel` 82 B y `SessionStats` 72 B. El estado de una mano
  completa cabe en ~300 B; lo demás es caché de conocimiento (tabla, modelo, histórico).
- El perfil decide **cuánto piensa** el bot (muestras de Monte Carlo, resolución del modelo
  de rival) y **cuánto recuerda**, no la legalidad: un `N20` juega el mismo póker con menos
  información. La estrategia de nivel por footprint es la misma que la de tablero
  ([BOARD_LEVEL_AND_STATS.md](../../guides/roadmap/BOARD_LEVEL_AND_STATS.md)).
- `plan_cards_memory(free_bytes)` elige el perfil mayor cuyo `planned_bytes()` cabe; si no
  cabe ni `N20`, devuelve `N20` como fallback.
- La detección de RAM libre es **backend-agnóstica** (modelo de memoria del engine o
  contador de arena), igual que en `eng::board`.

## 3. Representación y reglas (`core/`, `rules/`)

### 3.1 Carta, baraja y mesa

- La **carta** es un `u8`: `rank << 2 | suit`, con `rank` 0..12 (2..A) y `suit` 0..3. El
  índice de baraja es 0..51; el centinela es `kNoCard` (`0xff`). Sin `unsigned long long`.
- La **baraja** (`Deck`) es un array inline de 52 B más un contador de cartas vivas.
  `shuffle` es Fisher-Yates sobre el PRNG; `remove` saca cartas conocidas; `deal` reparte
  desde el final. Mismo PRNG + misma semilla ⇒ misma permutación en host y en Amiga.
- La **mesa** (`Table`) es un struct de tamaño fijo sin punteros: 2..10 asientos, tablero,
  baraja, apuesta viva, mínimo de subida, botón y asiento al turno.

### 3.2 Texas Hold'em No-Limit

- **Ciegas** y botón: en 3+ jugadores la ciega pequeña es botón+1 y la grande botón+2; en
  *heads-up* el botón pone la ciega pequeña. Preflop abre UTG (botón+3) o el botón en
  heads-up; postflop abre el primero activo a la izquierda del botón.
- **Acciones**: `Fold`, `Check`, `Call`, `Raise` (con la apuesta **total objetivo**) y
  `AllIn`. `legal_actions` devuelve solo acciones válidas para el asiento al turno.
- **Cierre de ronda**: cuando ningún asiento activo debe actuar (todos igualados y con
  acción pendiente resuelta). Una subida corta *all-in* no reabre la acción.
- **Botes laterales**: al showdown se reparten por **niveles de aportación** (`committed`).
  Cada capa forma un bote con los aportantes que llegan a ella; solo los no retirados son
  elegibles, y el bote se divide entre los mejores (fichas indivisibles al primero por
  posición a la izquierda del botón).
- Si todos menos uno se retiran, el que queda gana el bote sin mostrar. Si todos están
  *all-in*, se corre el tablero y se resuelve por showdown.

### 3.3 Evaluación de manos (`rules/hand_rank.hpp`)

- Algoritmo **de conteo**, no fuerza bruta: una pasada llena 13 contadores de rango y 4 de
  palo, y calcula el candidato de cada categoría para quedarse con el máximo. Evita las 21
  combinaciones de "5 de 7" y no necesita tablas grandes.
- El resultado es un `HandValue` (`u32`): categoría (4 bits) << 20 | hasta 5 desempates de
  4 bits. Comparar enteros da exactamente el orden de manos.
- Categorías: carta alta < pareja < doble pareja < trío < escalera < color < full < póker <
  escalera de color. El as puede ser alto o bajo (escalera A-2-3-4-5).
- Casos cubiertos por el test: color + escalera (gana color), full sobre color, empates por
  *kicker*, mejor de 7.

## 4. Equity y pot odds (`eval/equity.hpp`)

- **Equity Monte Carlo**: se reparten `samples` tableros/rivales aleatorios, se evalúa la
  mejor mano del héroe contra la de cada rival y se cuentan victorias y empates. El
  resultado va en **por mil** (`u16`), sin `float`.
- `equity_vs_random(hole, board, opponents, samples, rng)` es determinista por semilla y se
  reutiliza en la decisión y en la simulación.
- **Pot odds**: `to_call / (pot + to_call)` en por mil. El bot exige un colchón de estilo
  sobre las pot odds antes de igualar.
- **Heurística preflop** (`preflop_strength_permille`): aproxima Chen (pareja, cartas altas,
  *suited*, conectores) para el perfil `N20`, que no gasta muestras.

### 4.1 Rangos y tabla preflop (`eval/range.hpp`)

- **169 clases canónicas** de mano inicial (13 parejas + 78 *suited* + 78 *offsuit*) con
  índice simétrico y decodificación a combinaciones concretas (6/4/12 según clase).
- **`HandRange`** es un conjunto de clases (`BitSet<169>`, 24 B) con `combo_count`. El
  dealer de rango reparte la mano del rival desde el rango (clase uniforme, luego
  combinación), evitando cartas vistas.
- **`equity_vs_range`** estima el equity del héroe contra rivales restringidos a un rango
  (no mano aleatoria), reutilizando `equity_vs_dealer`.
- **`PreflopTable`** (342 B) guarda el equity heads-up de las 169 clases en por mil,
  construido una vez con Monte Carlo determinista (`build_preflop_table`) para no incrustar
  datos opacos; ordenado por equity genera rangos por percentil (`make_range_by_equity`).

## 5. Decisión y modelo de rival (`ai/bot.hpp`)

- **Estilo** (`BotStyle`, 5 variantes: tight/loose × passive/aggressive + balanced) fija
  VPIP, agresividad, farol, colchón y umbral de apuesta. El estilo es **ortogonal al
  footprint**: dos bots `N512` pueden jugar estilos opuestos.
- **Decisión**: sin apuesta viva, apuesta si la fuerza supera el umbral o farolea; con
  apuesta viva, compara fuerza con pot odds + colchón; muy fuerte ⇒ sube, rentable ⇒ iguala
  o sube según agresividad, débil ⇒ farol ocasional o se retira. La fuerza preflop sale de
  la tabla de 169 clases si el perfil la mantiene (convertida a multiway con
  `multiway_from_heads_up`); el Monte Carlo puede usar un **rango de rival** opcional.
- **Modelo de rival** (`OpponentModel`): acumula por asiento folds/calls/raises y expone
  frecuencias en por mil; el bot sube el farol ante rivales que se retiran mucho. Va
  dimensionado por `CardPlan::tracked_opponents`.

## 6. Simulación desde host (`sim/session.hpp`)

- `run_session(config, plan, stats, model)` juega `hands` manos con botón rotando y
  recompra por mano; devuelve net por asiento, `bb/100`, showdowns, retiradas y acciones.
- Determinista por semilla: misma configuración ⇒ mismos resultados. Es la herramienta para
  **ajustar el nivel** (estilos, umbrales, muestras) sin abrir WinUAE, y para detectar
  regresiones en las reglas (la conservación de fichas debe cumplirse siempre).

## 7. Decisiones de diseño

- **Carta `u8` y baraja inline**: 52 B, sin 64 bits ni asignaciones; todo el estado cabe en
  pila o arena estática.
- **`HandValue` empaquetado en `u32`**: comparación entera directa, sin `float` ni
  multiplicaciones; los rangos ocupan 4 bits y las categorías son crecientes.
- **Azar inyectado**: el PRNG común (`eng::Xoroshiro64pp`) con semilla hace reproducibles
  tests, análisis y partidas de torneo.
- **Zona de trabajo `cards_int`** (`eng::intw`: `s16` en 68000, `s32` en 68020, `int` en
  host) para acumuladores e índices; `s32` solo donde el rango lo exige (fichas, pot).
- **Interfaces seguras**: sin punteros crudos ni `char*` en la frontera. Buffers y manos
  como `Span`; texto como `StringView`; el estado es un struct de campos públicos de tamaño
  fijo; no hay excepciones, heap ni RTTI.
- **Sin duplicar**: la baraja se baraja con `eng::shuffle`; el azar es `eng::Xoroshiro64pp`;
  los perfiles de memoria siguen el patrón de `eng::board`; no se reinventan contenedores.
- **Aritmética sin libcalls** (`core/intmath.hpp`): el 68000 no tiene mul/div de 32 bits
  nativo. Los productos 16×16 usan `mulu16` (`mulu.w`) y la división `u32/u16` usa `divu.w`
  por mitades de 16 bits. `divmod32` es `noinline` a propósito: inlineado en un contexto con
  rango conocido, GCC reconoce el patrón y lo sustituye por `__divsi3` de libgcc. La sonda
  `tools/analyze/codegen-report.mjs` falla si aparece cualquier libcall o instrucción 68020.

## 8. Inventario

| Área | Estado |
|---|---|
| `core/` (tipos, baraja, presupuesto `N20`…`N512`) | **Implementado**: HOST-161 |
| `core/intmath.hpp` (división `divu.w` / `mulu16` sin libcalls) | **Implementado**: codegen-report (68000 sin libgcc) |
| `rules/hand_rank.hpp` (evaluador 5/7) | **Implementado**: HOST-162 |
| `rules/texas_holdem.hpp` (reglas, calles, acciones, side pots, showdown) | **Implementado**: HOST-163 |
| `eval/equity.hpp` (Monte Carlo, pot odds, heurística preflop) | **Implementado**: HOST-164 |
| `eval/range.hpp` (169 clases, rangos, tabla preflop) | **Implementado**: HOST-166 |
| `ai/bot.hpp` + `sim/session.hpp` (estilos, modelo de rival, sesiones) | **Implementado**: HOST-165 |
| Herramienta host `tools/cards/selfplay.sh` | **Implementado y ejecutado** (torneos CPU vs CPU) |
| Benchmark hardware `demos/amiga/124_cards_bench` | **Implementado y medido en A500**: `N20` ≈ 30 unidades/s, `N64` (8 muestras) ≈ 685 ms/unidad; `N128`+ no jugables. Muestras por perfil calibradas con esta tabla |
| Juego con UI en `games/` | **Pendiente** (los motores están **NO VERIFICADOS** en hardware) |

> Estado: núcleo, reglas, evaluación (equity/rangos), IA y simulación implementados y
> verificados por test host (HOST-161…166); el codegen 68000 está libre de libcalls y de
> instrucciones 68020. El juego con interfaz en el Amiga, el pulido visual y la medida de
> rendimiento por CPU quedan pendientes. El plan por fases y los criterios de cierre están en
> [ROADMAP_CARD_GAMES.md](../../guides/roadmap/ROADMAP_CARD_GAMES.md), fuente única del
> avance. Este documento describe el diseño vigente y no se duplica allí.
