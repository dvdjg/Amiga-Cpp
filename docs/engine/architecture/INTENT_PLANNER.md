# INTENT_PLANNER — el API por **intenciones** (no bloqueante)

Este documento fija **cómo se expresa una intención** en el engine y **cómo se ejecuta** sin
bloquear, con **callbacks/eventos** cuando la petición llega a su punto. Es el contrato del API de
alto nivel: el juego **declara qué quiere** y el engine **decide cómo y cuándo**.

Relacionado: [`BLITTER_INTENT_QUEUE.md`](BLITTER_INTENT_QUEUE.md) (la cola de blit por intención),
[`MINI_OS_MESSAGE_LOOP.md`](MINI_OS_MESSAGE_LOOP.md) (mensajes, puertos y despacho),
[`PUBLIC_API.md`](PUBLIC_API.md) §4 (planner) y [`OBJECT_SYSTEM.md`](OBJECT_SYSTEM.md) (los objetos).

## 1. Principio: declarar **no** es ejecutar

Declarar una intención —**dibujar un rectángulo**, **reproducir un sonido**, programar un cambio de
paleta— **no** dibuja, **no** suena y **no** bloquea: **se encola**. El engine decide la vía
(CPU/Blitter/Copper/Paula) y el momento (VBlank, ventana segura del raster), y **avisa** cuando la
cola llega a ese punto.

```text
   el juego declara            el engine compila              el hardware ejecuta
   ─────────────────           ──────────────────             ─────────────────────
   Draw{ rect }  ┐             intención → plan del           Blitter en la ventana
   Sound{ sfx }  ├─ enqueue ─► subsistema (blit/copper/  ─►   segura (IRQ/Copper);
   Copper{ … }   ┘  (NO wait)  audio) + SU PRESUPUESTO        Paula desde su IRQ
                               └── al llegar el punto → **evento** al puerto del juego
```

**Dos reglas de oro.** (a) *Nada bloquea salvo que se pida.* (b) *La completación es un **evento**,
no un sondeo ni un callback crudo en la ISR.*

## 2. Las tres capas (una responsabilidad por capa)

| Capa | Qué es | Quién la ve |
|---|---|---|
| **Intención** | el vocabulario del juego: `Draw`, `Sound`, `CopperIntent`… (una petición = un valor) | el juego (API pública) |
| **Plan** | el **sumidero**: los planes por subsistema (`FramePlan` para blits/copper, el plan de audio) con **presupuesto** y orden | el engine |
| **Ejecución** | el backend (registros, secuencia de 13 fases, mezclador) en los puntos del raster/IRQ | solo el backend |

El juego **solo** escribe la capa 1; las capas 2–3 son el engine. Un `BlitJob`/`BlitterJob`, un
`BPLxPT` o una fase del C2P **no** aparecen en la capa 1.

## 3. El vocabulario: una intención, un tipo

Una intención es un **valor pequeño y copiable** (cabe en una entrada de cola; sin punteros
propietarios), agrupada por **subsistema**:

| Subsistema | Intenciones (ejemplos) | Vía |
|---|---|---|
| **Blit** | `Draw::Rect` / `Draw::Line` / `Draw::Sprite(frame, x, y)` / `Draw::Text` | Blitter (ventana) o CPU |
| **Raster** | `CopperIntent` (paleta por línea, split, modo) | Copper |
| **Audio** | `Sound::Sfx(sample)`, `Sound::Music(module)` | Paula + mezclador |

Un **rectángulo es un rectángulo**: el juego no elige minterm ni canal; pide `Draw::Rect` y el
planner lo compila a `BlitOp` → `BlitJob` → `BlitterJob` (ver `BLITTER_INTENT_QUEUE.md`).
**Lo que no encaja en un tipo no va en su enum** (el caso C2P: es una **conversión**, no un blit).

## 4. La cola (no bloqueante)

```cpp
Ticket t = api.enqueue(intent);   // encola y SIGUE (no espera)
api.flush();                      // intenta avanzar; NO espera
api.wait(t);                      // SOLO si el juego lo pide (equivalente a glFinish)
```

- **Capacidad fija** (sin heap), **FIFO** por subsistema; IRQ-safe en la escritura/lectura.
- `enqueue` devuelve un **`Ticket`** (un entero generacional) que identifica la petición.
- El **único** bloqueo es `wait(ticket)` — y **solo** si el juego lo pide. También `wait_all()` en
  el commit del frame si la app lo necesita (el bucle normal del mini-SO **no** lo necesita).

### 4.1 Coste en el bucle: **compile-time y setup**, no por-frame

La penalización en el bucle principal ha de ser **mínima** (regla del API, `CODING_STYLE.md`):
el `update`/`render` por frame **no** admite sobrecarga (virtual, reservas, `switch` de modo,
consultas que se podrían resolver antes). Por eso:

- **Compilación**: lo fijo se resuelve con `consteval`/`constexpr`/`if constexpr`/plantillas y tablas
  (`ct_array`, `InvSqrtTable<…>`) — minterms, geometrías y descriptores **no** se recomputan por frame.
- **Setup de la escena**: lo invariante durante la escena se **compila una vez** (la "receta": capas,
  planes base, direcciones de plano, presupuestos) y el bucle **solo actualiza lo dinámico**
  (posiciones, frames, colores). Patrón: `DrawRecipe<N>` (lista fija de intenciones descrita en el
  setup) que el bucle **reproduce** con un recorrido mínimo — **cero asignación por frame**.
- **Expresión fusionada**: cuando una composición de operaciones deba resolverse en una sola pasada,
  **expression templates** (`eng/core/math/expr.hpp`), sin temporales.

Criterio: si una abstracción del API **cuesta ciclos por frame**, o se elimina en compilación/setup,
o no entra en la capa A.

## 5. La completación: **eventos del mini-SO** (el callback)

Cuando el engine **alcanza el punto** de una intención —aunque queden más peticiones por
ejecutar— **postea un evento**:

```cpp
Msg m;
m.type = MsgType::IntentDone;      // o el más específico: BlitDone/FileDone/…
m.payload.user.a = ticket.value;   // correlación: qué petición terminó
port.post(m);                      // al puerto del juego
```

El juego lo recibe en su **`HandlerTable`** (la tabla de despacho del mini-SO) o en el bucle:

```cpp
static void on_intent_done(Ctx& ctx, const Msg& m) {
        ctx.resolve(Ticket {m.payload.user.a});   // el callback de esa petición
}
```

**Por qué así y no un puntero a función suelto:** (a) es **IRQ-apto** (un `Msg` es trivial y
copiable en la ISR, §`message.hpp`); (b) **desacopla** (el feeder no llama código del juego
directamente); (c) **reutiliza** el mini-SO (ya hay `FileDone`/`BlitDone` con `cookie`); (d) el
juego decide **cuándo** atiende (su bucle, su prioridad). Un callback directo solo es admisible
fuera de la ISR (en el drenaje del bucle), y **nunca** en el feeder.

**Alternativas** (si el caso lo pide): una **`Task`** del mini-SO para completaciones con cuerpo
(`MINI_OS_TASKS.md`), o un `wait` explícito. El **evento** es la opción por defecto.

## 6. El sumidero único, versátil

Un **solo** sumidero recibe las intenciones de todos los productores y las gobierna:

```text
                 ┌──────────────────────────────────────────────┐
   juego ────────┤                                              │
   efectos ──────┤   PLANNER (sumidero)                         │  feeders (IRQ):
   audio ────────┤   · colas por subsistema + presupuesto        ├─ VBlank (VERTB)  → plan de blit/copper
   mini-SO ──────┤   · compila intención → plan                  ├─ BLIT IRQ        → drena la cola de blit
                 │   · emite IntentDone al llegar el punto       ├─ audio IRQ       → avanza el mezclador
                 └──────────────────────────────────────────────┘
```

- **Un vocabulario, varios planes.** El planner no ejecuta: **compila y reparte** por subsistema y
  **respeta el presupuesto** de cada uno (blits/palabras del frame, voces de audio, líneas de copper).
- **IRQ-aware.** Los feeders (VBlank, `BLITDone`, audio) **avanzan** las colas; la completación se
  emite **desde el drenaje**, no desde la ISR si el cuerpo no es trivial.
- **Políticas, no subsistemas.** CPU-ventana-segura y Copper son **políticas** de la misma cola
  (`BlitQueue<N, Executor>`); cambiar de vía **no** cambia el vocabulario.
- **Sin punteros a función ni `void*`** en el dominio: todo **tabla/plantilla** (CODING_STYLE).

## 7. Relación con lo que ya existe

- **`FramePlan`** es el **plan de blits/copper** del frame (el sumidero de la capa 2). El planner
  escribe en él; el backend lo ejecuta (`execute_frame_plan`).
- **`BlitQueue<N, Executor>`** es el **front-end asíncrono**; su `SinkBlitExecutor` ya vuelca al
  `FramePlan` (una sola ruta). El planner es la capa de **arriba** (vocabulario + completación).
- **`eng::os`** pone el **evento** y el **despacho**; **`eng::audio`** pone su plan/mezclador. El
  planner los **coordina** sin fundirlos.

### 7.1 Decisión: un **sumidero único**, vías como políticas

- **El `FramePlan` es *el* sumidero** de la capa 2: no hay un segundo plan paralelo. La vía
  (CPU-ventana / Blitter / Copper) es una **política** del ejecutor, nunca un subsistema aparte.
- **La cola es única y genérica**: `IntentQueue<N, Item, Executor, Done>`
  (`intent_queue.hpp`). El `BlitQueue` es su instancia con `Item = BlitOp`; un `SoundQueue` es la
  instancia con `Item = SoundIntent`. **Mismo mecanismo** (ticket + completación) para todos.
- **El audio es un `plan` análogo** (`AudioPlan`): reparto de voces (Sfx/Music), presupuesto por
  frame e IRQ (Paula) como feeders — la misma forma que el plan de blit. Hoy `eng::audio` ya tiene
  mezclador/reproductores; el `AudioPlan` es el **contrato** que los unifica con el planner.
- **Naming**: alinear los `concept`s de ejecutor (`ready`/`run` del `IntentQueue` frente a
  `blitter_free`/`submit` del `BlitQueue`) es el paso mecánico para que el `BlitQueue` **sea** un
  `IntentQueue` (hoy conviven con nombres distintos).

## 8. Qué **no** es

- **No** es una máquina de estados con hilos: es **cooperativo** sobre el bucle del mini-SO.
- **No** es un `std::function`/`variant`: son **valores triviales** + una **tabla** de despacho.
- **No** obliga a esperar: la ejecución normal **nunca** bloquea; `wait` es la excepción explícita.
- **No** expone el cómo: ni registros, ni fases, ni minterms en el vocabulario.

## 9. Criterios de aceptación

- Declarar una intención **no** bloquea (medible: el `enqueue` no espera al Blitter/Paula).
- La completación llega **como evento** al puerto, con el `ticket` correcto.
- El **mismo** vocabulario sirve con la vía CPU y la de Blitter/Copper (políticas).
- El juego **no** nombra hardware, planos, `BlitJob` ni tipos del backend.
- En un **presupuesto** excedido, el planner **rechaza/acumula** de forma controlada (nunca corrompe).
