# Roadmap del perfil psicológico de NPC (`eng::sim`, capa de persona)

Plan de crecimiento de la **capa de persona** de `eng::sim`: arquetipos, rasgos, aptitudes,
defectos, expresión no verbal, lectura de tells, evolución psicológica y convenciones
secretas (base del Mus). El diseño vigente (modelo, catálogos y fórmulas) está en
[NPC_PSYCHOLOGY.md](../../engine/architecture/NPC_PSYCHOLOGY.md) (fuente de referencia, no
se duplica aquí). Este documento es la **fuente única del avance**: qué falta, en qué orden
y cómo se verifica.

Relación con las otras familias: se apoya en `eng::sim`
([SIM_ECOSYSTEM.md](../../engine/architecture/SIM_ECOSYSTEM.md)) y lo **extiende** sin
duplicarlo (personalidad, mente, relaciones, comunicación y cuerpo ya existen). El primer
consumidor es `eng::cards` ([CARD_GAME_AI.md](../../engine/architecture/CARD_GAME_AI.md)).
La taxonomía de decisión es de `eng::ai`
([GAME_AI_LIBRARY.md](../../engine/architecture/GAME_AI_LIBRARY.md)).

## 1. Objetivo y criterio

Dotar al engine de personajes controlados por la máquina con **psicología creíble y
legible**: que tengan carácter (arquetipo), estado (tensión, tilt, racha), **fugas
expresivas** coherentes con lo que sienten y su control, y que los demás **aprendan a
leerlos** con el tiempo. Debe ser determinista, entero, sin heap y con footprint acotado
(apto para un A500), y aplicable a cualquier juego, no solo a las cartas.

## 2. Estado de partida

- **Implementado y reutilizable**: `eng::sim` aporta `Personality` (10 rasgos), `Mind` (12
  afectos + autonomía/deferencia), `MemoryEvent`/`MemoryKind`, `Relationship`/`bond_score`,
  `Senses`/`Tracker`/atención, `behavior.hpp` (utilidad), `communication.hpp` (señales) y
  `body.hpp` (postura). Verificado por HOST-152…175.
- **Implementado**: rasgos de psique/aptitudes/defectos y arquetipos (`psyche_traits.hpp`,
  `archetypes.hpp`, `persona.hpp`, HOST-199), expresión y fuga (`expression.hpp`, HOST-200),
  lectura de tells (`read.hpp`, HOST-201), evolución de partida (`psyche.hpp`, HOST-202),
  convenciones secretas (`convention.hpp`, HOST-203), postura desde gesto (`body.hpp`),
  integración con `eng::cards` (`cards/ai/persona_bot.hpp`, HOST-204), rango con tells,
  avatares con expresión en `games/200_holdem` y **humano como personaje**
  (`expression_from_input`, HOST-205).
- **Pendiente**: NPC reactivo al ritmo del humano en ajedrez/Go (P6.3), señales voluntarias
  del humano (P6.4) y escenarios de mesa en `docs/debugging/`.

## 3. Reglas transversales (criterios de aceptación)

- **Sin heap, entero, determinista**: rasgos y estado en `u8`, conversión con `trait_mod`/
  `apply_mod` (ya existentes), sin `float` ni libcalls de 32 bits.
- **Extiende, no duplica**: reutiliza `Personality`, `Mind`, `Relationship`, `Senses`,
  `Tracker`, `behavior.hpp`, `communication.hpp` y `body.hpp`; lo nuevo se apoya en ellos.
- **Paramétrico**: umbrales, pesos y costes de control por gesto viven en structs de
  parámetros (`ExpressionParams`, `PsycheParams`, `ReadParams`), no en el algoritmo.
- **Agnóstico de representación**: la expresión es lógica (canal, gesto, intensidad); el
  render decide cómo dibujarla. No conoce sprites.
- **Transversal**: nada de la capa de persona depende de `eng::cards`; el juego de cartas la
  consume como un caso particular.
- **Verificación**: `tests/host/NNN` con `README.md`, cruce `m68k` con la sonda de codegen
  (sin libcalls ni 68020) y, cuando exista, juego/demo como consumidor real; actualizar
  [NPC_PSYCHOLOGY.md](../../engine/architecture/NPC_PSYCHOLOGY.md) y este roadmap en la
  misma pasada.
- **Footprint medible**: sonda de tamaños por perfil, coherente con `N20`…`N512`.

## 4. Fases

### P0 — Rasgos y persona

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| P0.1 | `sim/psyche_traits.hpp` | `PsycheTraits` (20 rasgos `u8`), `Skills` (15 aptitudes), `Flaws` (16 defectos) | **HOST-199** (hecho) |
| P0.2 | `sim/persona.hpp` | `Persona` (rasgos base + psique + aptitudes + defectos + arquetipo) y `materialize` con jitter determinista | **HOST-199** (hecho) |
| P0.3 | `sim/archetypes.hpp` | Tabla `kArchetypes[]` con los ~39 arquetipos del catálogo y `archetype_of` | **HOST-199** (hecho) |

Cierre: una persona es una fila de tabla materializada; dos individuos del mismo arquetipo
difieren por jitter. La tabla cubre el catálogo de §4 de la referencia.

### P1 — Expresión no verbal

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| P1.1 | `sim/expression.hpp` | `ExpressionChannel`, `GestureKind` (catálogo de ~72 gestos) y `control`/`detect` por gesto | **HOST-200** (hecho) |
| P1.2 | `sim/expression.hpp` | `ExpressionParams` y `leak()`: emoción × compostura × control → intensidad de fuga por canal | **HOST-200** (hecho) |
| P1.3 | `sim/expression.hpp` | Microexpresiones (duración corta + detectabilidad) y composición de varios gestos | **HOST-200** (hecho) |
| P1.4 | `sim/body.hpp` (extensión) | `pose_from_gesture`: el gesto se refleja en la postura existente | Pendiente (mejora) |

Cierre: dado un estado, se calcula qué gestos se fugan y con qué intensidad; los canales
autonómicos delatan siempre, los volitivos se controlan.

### P2 — Lectura de tells

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| P2.1 | `sim/read.hpp` | `ReadModel` (TellStat por objetivo/gesto, exposición, incertidumbre) y `observe_tell` | **HOST-201** (hecho) |
| P2.2 | `sim/read.hpp` | `label_showdown` (aprende al revelarse) y `p_strong`/`indicio` (Bayes-lite entero) | **HOST-201** (hecho) |
| P2.3 | `sim/read.hpp` | `infer_archetype` (prior) y descuento por `suspicion` (tell invertido del listillo) | **HOST-201** (hecho) |
| P2.4 | `sim/senses.hpp` (integración) | La observación de tells usa visión + atención (`attention_score`) | Pendiente (mejora) |

Cierre: un observador aprende a leer a un pardillo tras N showdowns y no se fía de un
listillo; sin showdown, la incertidumbre persiste.

### P3 — Evolución psicológica de partida

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| P3.1 | `sim/psyche.hpp` | `PsycheState` (compostura, tensión, confianza, tilt, fatiga, ánimo, racha, imagen) | **HOST-202** (hecho) |
| P3.2 | `sim/psyche.hpp` | `TableEventKind` y `psyche_observe`: cada evento afecta a `Mind` y al estado (paramétrico) | **HOST-202** (hecho) |
| P3.3 | `sim/psyche.hpp` | `psyche_update` (deriva por pasos) y efecto sobre compostura/agresión/farol | **HOST-202** (hecho) |
| P3.4 | Escenarios | `docs/debugging/` con partidas controladas (pardillo, listillo, irascible, flemático) | Pendiente |

Cierre: una racha de derrotas lleva al irascible al tilt y deja al flemático estable; los
rivales ven cambiar su lectura.

### P4 — Convenciones secretas (base del Mus)

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| P4.1 | `sim/convention.hpp` | `Convention` (mapa gesto→señal, disimulo, exposición) y `emit`/`decode` | **HOST-203** (hecho) |
| P4.2 | `sim/convention.hpp` | `infer_convention` (repetición correlacionada) y riesgo de descubrimiento | **HOST-203** (hecho) |
| P4.3 | `sim/communication.hpp` (integración) | La señal de convención entra por `receive_signals`/`apply_signal_effect` | Pendiente (mejora) |

Cierre: dos jugadores comparten un código que los demás no decodifican; si se repite sin
disimulo, un observador puede inferirlo.

### P5 — Integración con `eng::cards` y juego

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| P5.1 | `eng::cards/ai/persona_bot.hpp` | `Persona`/`PsycheState` por asiento y `BotParams` derivados del arquetipo | **HOST-204** (hecho) |
| P5.2 | `eng::cards/ai/persona_bot.hpp` | `emit_tells`/`read_showdown`: emisión de tells y lectura por los rivales | **HOST-204** (hecho) |
| P5.3 | `eng::cards` | El rango dinámico combina línea de apuesta + tells leídos | **HOST-204** (hecho) |
| P5.4 | `games/200_holdem` | Avatares con expresión visible (caras y postura) y evolución en la partida | **build → run → analyze OK** (avatares con cara) |

Cierre: la mesa de póker muestra personajes que se delatan, se leen y evolucionan; el juego
es el consumidor real que retira el estado "NO VERIFICADA".

### P6 — Humano como personaje y expresión en juegos de información perfecta

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| P6.1 | `sim/expression.hpp` | `expression_from_input`: la entrada (gesto explícito + timing implícito) produce fugas del humano | **HOST-205** (hecho) |
| P6.2 | `sim/read.hpp` | El `ReadModel` se construye igual sobre asientos humanos (simetría de lectura) | **HOST-205** (hecho) |
| P6.3 | `games/100_chess` / `games/101_go` | NPC reactivo al ritmo del humano: bostezo si tarda, resoplido, impaciencia, sorpresa; catálogo de gestos por juego | Pendiente |
| P6.4 | `sim/communication.hpp` | El humano emite señales voluntarias (burlarse, amenazar, calmar) y el NPC las interpreta | Pendiente |

Cierre: el humano es un personaje más (sus gestos se leen y se emiten a propósito) y los
rivales de ajedrez/Go se comportan como contrincantes vivos, no como motores silenciosos.

## 5. Dependencias entre fases

```text
   P0 persona/arquetipos
        │
        ├──► P1 expresión ──► P2 lectura
        │                          │
        └──► P3 evolución ◄────────┘
                 │
                 ▼
            P4 convenciones (Mus)
                 │
                 ▼
            P5 integración con eng::cards y el juego
                 │
                 ▼
            P6 humano como personaje y expresión en ajedrez/Go
```

P0 es cimiento. P1 produce lo que P2 aprende a leer. P3 da la dinámica temporal que ambas
usan. P4 se apoya en P1–P2 (gesto + disimulo + inferencia). P5 cierra con el juego real. P6
hace bidireccional el sistema (el humano también emite y es leído) y lo lleva a los juegos
de información perfecta.

## 6. Distribución de tests host

Los números son únicos y no reutilizables; el siguiente libre es **199** (bloque D,
`feature/optimize`; ver [NUMBERING.md](../../ai-dev-environment/NUMBERING.md)). Antes de
crear cada pieza se comprueba que no duplica una primitiva de `eng::sim`/`eng::util`.

| Test | Cubre | Estado |
|---|---|---|
| HOST-199 | `psyche_traits.hpp` + `persona.hpp` + `archetypes.hpp`: rasgos, aptitudes, defectos y materialización por arquetipo | **Hecho** |
| HOST-200 | `expression.hpp`: canales, control por gesto, fuga y microexpresiones | **Hecho** |
| HOST-201 | `read.hpp`: aprendizaje de tells, Bayes-lite, prior de arquetipo y suspicacia | **Hecho** |
| HOST-202 | `psyche.hpp`: estado, eventos de mesa y evolución (tilt/racha/compostura) | **Hecho** |
| HOST-203 | `convention.hpp`: emisión/decodificación, disimulo e inferencia de convención | **Hecho** |
| HOST-204 | `eng/cards/ai/persona_bot.hpp`: parámetros por arquetipo/estado, tells, lectura y rango con tells | **Hecho** |
| HOST-205 | Humano como personaje: `expression_from_input` (gesto explícito + timing) y lectura simétrica | **Hecho** |
| HOST-206+ | Avatares en ajedrez/Go (P6.3), señales voluntarias del humano y escenarios de mesa | Pendiente |

## 7. Decisiones y descartado

- **Arquetipos como tabla, no como `enum` cerrado**: permite añadir personalidades sin
  tocar código y combinar rasgos; el `enum` solo etiqueta el arquetipo dominante.
- **Aptitudes separadas de los rasgos**: la pericia se adquiere y se pierde; el carácter no.
  Un pardillo puede volverse experto sin dejar de ser pardillo.
- **Fuga como multiplicación, no como azar**: la expresión es función determinista del
  estado y el control; el ruido solo desempata. Nada de "gesto aleatorio".
- **Microexpresión = tiempo corto + atención del observador**: modela que solo el que mira
  la caza; no es un gesto distinto.
- **El tell se aprende en showdown**: sin etiqueta no hay aprendizaje; de ahí "conocerle
  bien". Es el mecanismo que separa al pardillo del listillo.
- **Perfiles físicos (fuerza, resistencia) fuera de alcance**: van con genética/cuerpo de
  `eng::sim`, para otros juegos.
- **Reglas del Mus fuera de alcance**: aquí se define el sustrato (convenciones, disimulo,
  descubrimiento), no el juego.
- **El humano es un personaje, no un observador**: el sistema es **bidireccional**; los NPC
  leen sus gestos (explícitos y de *timing*) igual que él lee a los NPC, y él puede emitir
  señales voluntarias. Aplica a póker, ajedrez, Go y el Mus.
- **Catálogo de gestos por juego**: los mismos canales sirven a todos, pero qué gestos son
  relevantes (un `yawn` en un tablero, un `tell` en el póker) lo configura el juego.

## 8. Cómo se cierra cada paso

1. Cabecera en `engine/include/eng/sim/<área>.hpp` con comentario didáctico (intención,
   coste, límites y ejemplo de uso).
2. `tests/host/NNN` + `README.md` y registro en el catálogo.
3. Sonda de codegen para los caminos calientes (sin libcalls ni instrucciones 68020) y sonda
   de tamaños por perfil.
4. Actualizar [NPC_PSYCHOLOGY.md](../../engine/architecture/NPC_PSYCHOLOGY.md) (inventario/
   estado) y este roadmap (fase hecha) en la misma pasada.
5. Commit atómico por paso, con la referencia de la técnica y la del test.
