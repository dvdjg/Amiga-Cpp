# Ergonomía del API: criterios del usuario

Este documento fija las **preferencias de diseño del usuario del engine**: qué se considera «elegante» y qué no al tocar la fachada de juego, los ejemplos y el bootstrap. Complementa a [PUBLIC_API.md](PUBLIC_API.md) §1.1 (intuitivo, simple, sin restringir) y [CODING_STYLE.md](CODING_STYLE.md), y es criterio de aceptación al revisar una API nueva aunque el código funcione.

El objetivo no es que la demo tenga el menor número de líneas posible, sino que **su lógica principal se lea sin conocer el chipset**: se aceptan más líneas si están justificadas por funciones reutilizables con nombres expresivos.

## 1. La app no arranca la máquina

- El código de juego/demo **no** incluye `support/gcc8_c_support.h`, no declara `struct ExecBase* SysBase`, no define `g_eng_run_status` ni escribe un `main()` de plataforma, y no incluye `<proto/exec.h>` ni `<exec/execbase.h>`.
- Eso lo pone el bootstrap de plataforma: `eng::amiga::run_demo(demo, display)` (memoria del juego + `start` + bucle + telemetría del runner) o `ENG_APP_MAIN(Game)` para composición manual de escena (`eng/platform/amiga/entry.hpp`). El display se declara **imperativamente** en `main` (`GameDisplay display {}; display.width = …;`), no con un callback `display()`.
- `SysBase` vive una sola vez en `engine/src/platform/amiga/amiga_boot.cpp` y se inicializa en un constructor previo a `main`; las demos no repiten `*reinterpret_cast<struct ExecBase**>(4UL)`.
- Los detalles de depuración (`mark_init_started`, `probe_when_ready`, telemetría, códigos de etapa) son del bootstrap: el juego solo pide `eng::amiga::mark_ready()` cuando su puesta en escena está completa.

## 2. Cero números mágicos en la interfaz

- Nada de `0xffffu`, `0x00012201u`, `0x2c81`, `0x5200` ni índices crudos en la lógica de juego. Los números mágicos se sustituyen por:
  - **valores por defecto** (`app.run()` en vez de `app.run(0xffffu)`);
  - **enums tipados** (`eng::debug::BootFail`, `RunState`, `RasterOp`, `ScrollKind`, `BobDraw`…);
  - **`constexpr` con nombre** para geometría, umbrales y tamaños reutilizados;
  - un **comentario de línea** cuando el literal es local y el nombre añadiría ruido (AGENTS §1.16).
- Los códigos de `detail` del runner son un contrato de depuración: el juego usa `mark_ready()`/`BootFail` y no escribe valores hexadecimales propios.

## 3. Vocabulario de intención, no de chipset

- Las llamadas se leen como frases del dominio: `screen.circle(cx, cy, r, color)`, `debug::draw_hud(screen, "CIRCLES", version, frame)`, `world.add_layer(...)`.
- Prohibido en la firma o en la lógica de juego: `BPLCON*`, `DMACON`, `Bitplane`, `planes`, `stride`, punteros, `BlitJob`, `FramePlan`, `Copper`/`CopperList`, `Block<Tag>`.
- Si algo obliga a bajar de nivel, es que **falta una abstracción**: se añade al engine (como `Screen::circle` o `draw_hud`) y la demo la usa.

## 4. Declarar qué, no cómo

- El juego declara su display (`GameDisplay`), paleta, color, cámara y objetos; el engine decide composición, copper, DMA, planos y buffers.
- Las pistas de presupuesto son abstractas («este efecto es caro, tolero bajar a 25 fps»), nunca registros ni ciclos.

## 5. Reutilizar antes que repetir

- Toda ayuda que aparezca en dos demos se sube al engine y se usa desde la fachada: `run_demo`, `debug::draw_hud`, `Playfield::draw_circle`, `Screen::text_words`, `Sprite`, `ScrollSpec`…
- Antes de escribir, buscar en el engine (AGENTS §1.7); el gate `prior-art` exige declarar la búsqueda previa.
- Se prefiere la demo **expresiva** (más líneas, cero plomería) a la demo corta que vuelve a montar memoria, copper o punteros.

## 6. Los detalles de depuración son del bootstrap y del engine

- El `RunStatus`, el `detail`, el frame counter, el `probe` del runner y el HUD por defecto no son API de juego.
- El juego solo expresa **lógica**; si necesita instrumentación propia, usa las utilidades del engine (`draw_hud`, telemetría) y no símbolos de plataforma.

## 7. Checklist al diseñar API nueva

1. ¿La llamada se entiende sin conocer Amiga? ¿Se lee como una frase del dominio?
2. ¿Hay algún número, registro, puntero o banco que el juego deba escribir? → elimínalo.
3. ¿Basta con la fachada (`eng/api/api.hpp`) o fachada + `entry`? Si hace falta más, ¿se puede envolver?
4. ¿Dos demos repetirían esto? → súbelo al engine y documenta su contrato.
5. ¿Existe una forma parcial que permita un estado incorrecto? → ciérrala (AGENTS §1.10).
6. ¿El coste en runtime es ~cero? Si no, justifícalo con medida (AGENTS §1.15).

## 8. Referencias y ejemplos

- Principios y frontera: [PUBLIC_API.md](PUBLIC_API.md) §1.1 y §12.
- Objetivo de juego: [PUBLIC_GAME_API.md](PUBLIC_GAME_API.md).
- Bootstrap: `engine/include/eng/platform/amiga/entry.hpp`, `engine/src/platform/amiga/amiga_boot.cpp`.
- Ejemplo canónico de demo ergonómica: `demos/techniques/amiga/effects/122_circles` (port de `circles`: `main` con `GameDisplay` imperativo + `run_demo`, `screen.circle`, `debug::draw_hud`; sin `SysBase`, sin `gcc8_c_support.h`, sin hex).
- Coste y estilo: [CODING_STYLE.md](CODING_STYLE.md), [`METODO_OPTIMIZACION.md`](../../guides/optimization/METODO_OPTIMIZACION.md).
