# Roadmap de motores de juegos de naipes (`eng::cards`)

Plan de crecimiento de `engine/include/eng/cards/`: motores de **juegos de naipes con
información oculta** para Amiga 500–1200 (68000/68020/68030), empezando por **póker Texas
Hold'em No-Limit**, con **footprint de 20 kB a 512 kB**. El diseño vigente (capas,
representación, presupuesto, reglas, evaluación, decisión y simulación) está en
[CARD_GAME_AI.md](../../engine/architecture/CARD_GAME_AI.md) (fuente de referencia, no se
duplica aquí). Este documento es la **fuente única del avance**: qué falta, en qué orden y
cómo se verifica.

Relación con las otras familias: `eng::board` (información perfecta) y `eng::ai` (tiempo
real) se documentan en sus roadmaps. Las tres reutilizan `eng::util`, `eng::math` y
`eng::Xoroshiro64pp`, y no se duplican.

## 1. Objetivo y criterio

Dotar al engine de un motor de póker **legal y con buen nivel** en hardware de 1987–1992.
El nivel se ajusta por **perfil de memoria** (muestras de equity, modelo de rival) y por
**estilo**, no introduciendo errores artificiales. El motor debe ser determinista por
semilla, sin heap, sin `float` y portable host↔Amiga. Los test host simulan partidas
CPU vs CPU para ajustar el nivel; el juego con UI en el Amiga cierra la verificación.

## 2. Estado de partida

- **Implementado**: núcleo (`core/`: tipos de carta, baraja determinista, presupuesto
  `N20`…`N512` y aritmética sin libcalls `core/intmath.hpp`), reglas (`rules/hand_rank.hpp`
  evaluador de 5/7 y `rules/texas_holdem.hpp` con ciegas, calles, acciones legales, botes
  laterales y showdown), evaluación (`eval/equity.hpp`: Monte Carlo, pot odds y heurística
  preflop; `eval/range.hpp`: 169 clases, rangos y tabla preflop), IA (`ai/bot.hpp`: estilos y
  modelo de rival, con **rango derivado de la línea de apuesta**) y simulación
  (`sim/session.hpp`). **Variantes**: Omaha de extremo a extremo, estructura Limit,
  **Seven-Card Stud** (`rules/seven_stud.hpp`) y **Five-Card Draw / Deuces Wild**
  (`rules/five_draw.hpp`), más **comodines** (mazo de 54 y rangos comodín). Verificado por
  HOST-161…171.
- **Implementado (juego)**: `games/200_holdem` (Texas Hold'em No-Limit, jugador + 2 bots
  con perfil `N20`); build → run → analyze **OK** en emulador.
- **Implementado (herramienta)**: `tools/cards/selfplay.sh` juega torneos CPU vs CPU en host
  con perfiles, estilos, tabla preflop y rango de rival, y reporta net/bb-100. Regresión de
  nivel con `tools/cards/regression.sh` + línea base, integrada en `run-host-tests.sh`.
- **Verificado el target 68000**: `tools/analyze/codegen-report.mjs` compila las rutas de
  cartas (evaluador, equity, reglas incluidas Stud/Draw, rangos, IA y simulación) sin
  libcalls de libgcc ni instrucciones 68020, y fija los `sizeof` reales.
- **Primitivas reutilizadas**: `eng::Xoroshiro64pp` y `eng::shuffle`
  (`eng/core/random.hpp`, HOST-100), `eng::Span`/`StringView`/`StaticVector`
  (HOST-073…079), `eng::parallel` (HOST-137) y el patrón de presupuesto de `eng::board`
  (HOST-139).
- **Pendiente**: pulido visual del juego y UI de variantes, medida de rendimiento por
  CPU/perfil, torneos con ciegas crecientes, port a asm de las rutinas calientes y backends
  de conocimiento externo si se quieren libros/rangos en disquete.
- **Verificación en hardware**: `games/200_holdem` (Texas Hold'em No-Limit, N20) es el
  consumidor real; corre en emulador (build → run → analyze OK). El benchmark
  `demos/amiga/124_cards_bench` mide el coste por perfil en A500.

## 3. Reglas transversales (criterios de aceptación)

- **Sin heap**: mesa, baraja, modelos y acumuladores se instancian en memoria estática,
  dimensionada por `CardPlan`.
- **Determinismo**: sin `float`; el azar entra solo por `eng::Xoroshiro64pp` con semilla.
  Las sesiones de test son reproducibles byte a byte.
- **Legalidad**: toda decisión del bot sale de `legal_actions`; la conservación de fichas se
  cumple siempre (suma de stacks constante dentro de una mano).
- **Footprint medible**: cada perfil (`N20`…`N512`) se cierra con el tamaño real de las
  estructuras (sonda `Show<sizeof(T)>` en el cruce `m68k`) y se registra la matriz de memoria.
- **Sin libcalls ni 68020 en rutas calientes**: equity y evaluación usan enteros y
  desplazamientos; donde haga falta multiplicar/dividir, `eng::math::mulu16` o resta
  repetida, igual que en `eng::board` (§12 de
  [OPTIMIZACION_GPP_68000.md](../optimization/OPTIMIZACION_GPP_68000.md)).
- **Concurrencia abstracta**: si se paraleliza la evaluación de candidatos, con
  `eng::parallel`, no-op en m68k y sin alterar el resultado (HOST-137).
- **Interfaces seguras**: sin punteros crudos ni `char*`; `Span`, `StringView`, structs de
  tamaño fijo, sin excepciones/RTTI/heap.
- **No duplicar**: reutilizar PRNG, utilidades y patrón de presupuesto antes de crear.
- **Verificación**: `tests/host/NNN` con `README.md`, cruce `m68k`, y juego en `games/` como
  verificación de hardware; actualizar [CARD_GAME_AI.md](../../engine/architecture/CARD_GAME_AI.md)
  (inventario) y este roadmap en la misma pasada.

## 4. Fases

### C0 — Núcleo, baraja y presupuesto

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| C0.1 | `core/types.hpp` | `Suit`, `Rank`, `Card` (`rank<<2\|suit`), `Street`, `Hand`, `HandValue` | **HOST-161** (hecho) |
| C0.2 | `core/deck.hpp` | Baraja de 52/54 cartas (comodines), `shuffle`/`remove`/`deal` sobre el PRNG común | **HOST-161/168** (hecho) |
| C0.3 | `core/budget.hpp` | Perfiles `N20`…`N512` y `plan_cards_memory` | **HOST-161** (hecho) |

Cierre: tipos y presupuesto verificados; baraja reproducible por semilla.

### C1 — Reglas de póker

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| C1.1 | `rules/hand_rank.hpp` | Evaluador de 5/7 por conteo, mejor de 5 entre 7, escalera de as bajo | **HOST-162** (hecho) |
| C1.2 | `rules/texas_holdem.hpp` | Ciegas, botón, calles, acciones legales y secuencia hasta el showdown | **HOST-163** (hecho) |
| C1.3 | `rules/texas_holdem.hpp` | Botes laterales por niveles y resolución por retirada | **HOST-163** (hecho) |
| C1.4 | `rules/variants.hpp` + `hand_rank.hpp` | Omaha (2 de 4 + 3 de 5) y estructura Limit; **comodines** (mazo 54) | **HOST-167/168/169** (hecho) |

Cierre: manos y categorías correctas; botes laterales y showdown verificados con casos
controlados; Omaha y Limit jugables de extremo a extremo y comodines soportados. Pendiente:
Seven-Card Stud y torneos con ciegas crecientes.

### C2 — Evaluación y equidad

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| C2.1 | `eval/equity.hpp` | Equity Monte Carlo determinista contra rivales aleatorios | **HOST-164** (hecho) |
| C2.2 | `eval/equity.hpp` | Pot odds y heurística preflop (fallback `N20`) | **HOST-164** (hecho) |
| C2.3 | `eval/range.hpp` | 169 clases canónicas, `HandRange` y `equity_vs_range` | **HOST-166** (hecho) |
| C2.4 | `eval/range.hpp` | `PreflopTable` (169 valores, MC una vez) y rango por percentil | **HOST-166** (hecho) |

Cierre: el equity ordena manos conocidas (AA > 72o), una mano hecha gana siempre, las pot
odds son correctas y el rival puede restringirse a un rango. Pendiente: rangos de subida
dinámicos (leídos de las acciones) y más resolución para perfiles altos.

### C3 — IA y modelo de rival

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| C3.1 | `ai/bot.hpp` | Estilos tight/loose × passive/aggressive + balanced y decisión por equity/pot odds | **HOST-165** (hecho) |
| C3.2 | `ai/bot.hpp` | `OpponentModel`: frecuencias por asiento y ajuste del farol | **HOST-165** (hecho) |

Cierre: dos estilos producen planes distintos y la actividad (subidas/showdowns) es visible.
Pendiente: rangos de subida, farol inducido por el tablero y equilibrio (bluff/value ratio).

### C4 — Simulación y ajuste de nivel

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| C4.1 | `sim/session.hpp` | `run_session`: N manos, botón rota, net y `bb/100` | **HOST-165** (hecho) |
| C4.2 | `tools/cards/selfplay` | Herramienta host de torneos CPU vs CPU (perfiles, estilos, tabla y rango) | **Hecho** (ejecutada); **regresión de nivel** `regression.sh` + línea base, integrada en `run-host-tests.sh` |
| C4.3 | Matriz de rendimiento | Manos/s y muestras/s por CPU (68000/020/030) y perfil | **A500 completo**: `demos/amiga/124_cards_bench` mide por TOD (50 Hz) — `N20` 30 u/s (33 ms), `N64` 1 u/s (641 ms), `N128` 2,75 s, `N256` 5,09 s, `N512` 44,6 s por unidad. Solo N20/N64 viables en 68000. 020/030 pendientes |

Cierre: el nivel se ajusta desde host sin emulador; la matriz fija qué perfil es jugable en
un A500 base. Resultado: **solo N20 y N64 son viables en A500**; `N128`+ quedan para máquinas
ampliadas. Las muestras por perfil de `core/budget.hpp` se recalibraron con esta matriz (de
32/96/224/448 a 8/16/32/64).

### C5 — Juego en hardware

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| C5.1 | `games/200_holdem` | Mesa de póker jugable: cartas, apuestas, bote y menú de acciones | **build → run → analyze OK** (captura con contenido) |
| C5.2 | UI y explicación | Resalte de la mano propia, pot odds visibles, explicación por plantillas | Pendiente |
| C5.3 | Medida y pulido | Perfil por RAM libre, tiempos de decisión y pulido visual | Pendiente |

Cierre: las APIs dejan de estar "NO VERIFICADAS" y el roadmap se marca completo por fase.
C5.1 cerrado (el juego corre en emulador con N20); C5.2/C5.3 pendientes.

## 5. Dependencias entre fases

```text
   C0 núcleo/baraja/budget
        │
        ▼
   C1 reglas (evaluador + holdem) ──► C2 evaluación/equity
        │                                   │
        │                                   ▼
        │                              C3 IA/modelo de rival
        │                                   │
        ▼                                   ▼
   C4 simulación/ajuste ◄───────────────┘
        │
        ▼
   C5 juego y verificación en hardware
```

C0 es cimiento. C1 fija la legalidad (sin ella no hay estado válido). C2 y C3 son el nivel de
juego. C4 ajusta el nivel desde host y fija el rendimiento por perfil. C5 cierra con
verificación real.

## 6. Distribución de tests host

Los números son únicos y no reutilizables; el siguiente libre es **172**. Antes de crear cada
pieza se comprueba que no duplica una primitiva de `eng::util`/`eng::parallel`.

| Test | Cubre | Estado |
|---|---|---|
| HOST-161 | `core/`: tipos, baraja determinista y presupuesto `N20`…`N512` | **Hecho** |
| HOST-162 | `rules/hand_rank.hpp`: categorías, orden, mejor de 7 y kickers | **Hecho** |
| HOST-163 | `rules/texas_holdem.hpp`: reparto, calles, retirada y botes laterales | **Hecho** |
| HOST-164 | `eval/equity.hpp`: Monte Carlo, heurística preflop y pot odds | **Hecho** |
| HOST-165 | `ai/bot.hpp` + `sim/session.hpp`: conservación, determinismo y perfiles | **Hecho** |
| HOST-166 | `eval/range.hpp`: 169 clases, `HandRange`, equity vs rango y tabla preflop | **Hecho** |
| HOST-167 | `rules/variants.hpp` + `evaluate_omaha`: Omaha 2+3, reparto y estructura Limit | **Hecho** |
| HOST-168 | Comodines: mazo de 54, sustitución en `evaluate_hand` y showdown | **Hecho** |
| HOST-169 | Omaha de extremo a extremo: equity de 4 cartas y `run_session` | **Hecho** |
| HOST-170 | `rules/seven_stud.hpp`: ante, bring-in, 5 calles, showdown y retirada | **Hecho** |
| HOST-171 | `rules/five_draw.hpp` + `evaluate_deuces_wild`: descarte y Deuces Wild | **Hecho** |
| HOST-172+ | Rangos de subida, torneos con ciegas crecientes y UI de variantes | Pendiente |

## 7. Extensiones, decisiones tomadas y descartado

- **Decisiones fijadas** (detalle en [CARD_GAME_AI.md](../../engine/architecture/CARD_GAME_AI.md)
  §7): carta `u8`; `HandValue` empaquetado en `u32`; azar inyectado con
  `eng::Xoroshiro64pp`; zona de trabajo `cards_int`; botes laterales por niveles de
  aportación.
- **Aritmética sin libcalls** (`core/intmath.hpp`): productos 16×16 con `mulu16` y división
  `u32/u16` con `divu.w` por mitades; `divmod32` es `noinline` porque GCC inlineado reconoce
  el patrón y lo cambia por `__divsi3`. El gate de codegen falla si aparece una libcall.
- **Variantes**: **Omaha** de extremo a extremo (`evaluate_omaha` 2+3, `equity_vs_random_omaha`
  y bot con 4 cartas) y **Limit** (apuesta fija con tope) implementados como políticas sobre
  el mismo evaluador y presupuesto; `PokerVariant`/`BettingStructure` (`rules/variants.hpp`).
  Pendiente: **Seven-Card Stud** (bring-in, cartas descubiertas) y torneos con ciegas crecientes.
- **Comodines**: soportados con un mazo de 54 cartas (`Deck::reset(true)`); `evaluate_hand`
  sustituye cada joker por la mejor carta posible (coste `O(52^wilds)`, para 1–2 comodines en
  el showdown). Los rangos de 169 clases son de Hold'em, así que en Omaha/comodines el rival
  del Monte Carlo es aleatorio.
- **Equity vs rangos**: extensión de C2; los rangos de arranque son un conjunto de 169
  manos canónicas, no una tabla de póker profesional (footprint acotado).
- **Redes neuronales / CFR profundo**: descartados; el objetivo es el comportamiento
  clásico fuerte (equity + pot odds + estilo + modelo de rival) en pocos kB.
- **Azar extraíble**: el motor no toca hardware de azar; la semilla la fija el llamador (en
  el juego, derivada del RTC o del input del usuario).
- **Dinero/fichas**: todas las magnitudes son enteras (`s32`); no hay `float` ni moneda real.

## 8. Cómo se cierra cada paso

1. Cabecera en `engine/include/eng/cards/<área>/` con comentario didáctico (intención, coste,
   límites por plataforma y ejemplo de uso).
2. `tests/host/NNN` + `README.md` y registro en el catálogo.
3. Sonda de codegen para los caminos calientes (sin libcalls ni instrucciones 68020) y sonda
   de tamaños por perfil.
4. Actualizar [CARD_GAME_AI.md](../../engine/architecture/CARD_GAME_AI.md) (inventario/estado)
   y este roadmap (fase hecha) en la misma pasada.
5. Commit atómico por paso, con la referencia de la técnica y la del test.
