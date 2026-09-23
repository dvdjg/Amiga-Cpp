# Bug de codegen gcc 15 m68k a `-O1`: `os::add_timer` con periodo > 1 no llega al `App`

## Síntoma

La fachada `eng::os` entrega los `Timer` de un timer periódico al `App` cuando el periodo es **1**,
pero **no** cuando es **2 o más**: el backend postea los mensajes (`TimerService::poll_and_post`
dispara) y `MessagePumpGame::pump_messages` los drena, pero los contadores **miembro** del `App` no se
actualizan. El síntoma observable en la demo 212 (`add_timer(1u, 2u)`) es `msgs`/`timers` = 0.

## Aislamiento

- `game.port.get() == &os::system_port()` ⇒ el pump y el backend usan el mismo `MsgPort`.
- Un `post` manual de `MsgType::Timer` desde `on_frame` (posterior al pump) **sí** se entrega.
- El pump **llama** a `on_msg` (un contador **global** sube), pero los contadores **miembro** del `App`
  no cambian ⇒ `on_frame` recibe un `this` distinto del que ve `on_msg` (y de `&game.app`).
- `TimerService` está validado en host (**HOST-222**, incluye periodo 2) y **HOST-309** fija el
  contrato hook→pump (periodo 1 y 2) ⇒ **la lógica del mini-SO es correcta**.

## Causa: perfil de optimización

El perfil `--debug` compila a **`-O1`** (`tools/build/build-demo.sh:183`); el `--release`, a `-O2`.

```text
--debug   (-O1) : msgs=0,  timers=0   (bug)
--release (-O2) : msgs=16, timers=16  (correcto; state=3)
```

Es un **bug de codegen de gcc 15 m68k a `-O1`** en el camino inlineado
`Engine::run_frames_polling` → `MessagePumpGame::update` → `pump_messages` → `on_frame`. El `.s` no
sirve para bisecar la instrucción: **todo queda inlineado en `main`** (no hay símbolos de `update` ni
`run_frames_polling` en el ensamblador).

## Workarounds probados (ninguno lo arregla)

- contadores del `App` `volatile`;
- referencia local nombrada para el `App` (`App& self = app; self.on_frame(...)`);
- reordenar `on_frame` **antes** del pump;
- `__attribute__((always_inline))` en `pump_messages`;
- barrera de compilador `asm volatile("" ::: "memory")` antes de `on_frame`;
- desligar el `TaskSystem` (`bind_tasks`), por si `run_idle` clobberaba el registro;
- flags `-fno-omit-frame-pointer` y `-fno-strict-aliasing` (además de `-O1`).

## Mitigación

```bash
DEMO_OPT=-O2 bash tools/build/build-demo.sh <demo> --debug
```

Compila el TU del demo a `-O2` dentro del perfil debug y **el bug desaparece** (`msgs`=16,
`timers`=16). Decisión: mantener las demos a **periodo 1** (estado verde); si una demo necesita un
timer de periodo > 1, compilar su TU a `-O2` o fijar/reportar el bug de gcc.

## Repro

La demo 212 con `eng::os::add_timer(1u, 2u)` es el repro reproducible (`demos/amiga/212_message_loop`).
Minimizado, el patrón es: un `MessagePumpGame<App>` con **un contador miembro** que se incrementa en
`on_msg` cuando llega un `MsgType::Timer`, un `add_timer(id, 2)` y lectura del contador en `on_frame`.

## Pendiente

- Aislar el flag/pase exacto que lo evita (probado: `-O2` sí; `-fno-omit-frame-pointer` y
  `-fno-strict-aliasing` no).
- Reportar a gcc con un repro mínimo autocontenido.
