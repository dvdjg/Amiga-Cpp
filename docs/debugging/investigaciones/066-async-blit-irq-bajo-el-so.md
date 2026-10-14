# 066: bugs del camino de blits encontrados con el showcase (todos cerrados)

Estado: **cerrado**. Cuatro bugs reales encontrados y corregidos durante la vitrina de BOBs.
Este doc sustituye a la hipótesis inicial "el SO mata la IRQ BLIT" (incorrecta: el handler de
nivel 3 del engine no encadena la ROM, `amiga.cpp:288-306`).

## Bug 1 — Caché de registros comunes del Blitter cruzaba jobs/frames (motor)

`m_blt_common_valid` sobrevivía entre jobs. Cualquier otro código (servidores del SO) puede
programar el Blitter en cualquier hueco; un job "igual" al cacheado se saltaba las escrituras y
bliteaba con registros ajenos → **planos perdidos**. Corregido: invalidación **por job** en
`submit_blit_job` (`amiga_blitter.cpp`). Las rachas batch fijan su estado explícitamente.

## Bug 2 — Recursión del feeder async por el servicio del `wait_blitter` (motor)

`wait_blitter()` llama al servicio de blit registrado; en async ese servicio ES el feeder →
un job multi-plano se re-entraba a sí mismo (cadena corrupta). Corregido con guard
anti-recursión (`g_in_blit_service`, `amiga_internal.hpp`).

## Bug 3 — La cadena avanzaba una vez por PLANO, no por JOB (motor)

Un job multi-plano lanza un blit por plano y **cada fin de blit dispara la IRQ BLIT**: el
feeder avanzaba 2-4 índices por job (saltaba jobs y reprogramaba a media cadena de planos) →
render corrupto. Corregido con un **gate de INTENA**: se desarma BLIT y se limpia el latch de
INTREQ durante la programación del job y se rearma al final; el latch (independiente de
INTENA) entrega **exactamente una IRQ por job** (`blit_irq_gate_begin/end`). Más guard de IRQ
espuria (`if (!m_async_busy) return;`).

## Bug 4 — `run_frames` ya había instalado otro servicio de blit (demo/motor)

`Engine::run_frames` instala el `BackgroundPump` como servicio de blit; `install_blit_service`
rechaza el segundo (`amiga.cpp:367-370`) → `execute_frame_plan_async` fallaba **en silencio**
cada frame y los BOBs **nunca se dibujaban** (la escena parecía "congelada": solo arte estático
+ sprites). Corregido en la demo con `engine.set_blit_service_enabled(false)` antes de
`run_frames`, más degradación a `execute_frame_plan` si el async devuelve false. Diagnóstico
usado: contadores `g_blit_async_*` (ahora accesibles vía
`AmigaBackend::blit_async_launches()/…`) pintados en la franja: **L = 1 launch/frame,
S = 17 submits/frame** con la cadena correcta.

## Bug 5 (demo) — `BPLCON0` hardcodeado a 5 planos

El display emitía `BPLCON0 = 0x5200` (BPU=5) tras reducir la demo a 4 planos → el 5º plano
leía `BPL5PT` (basura) → colores 16-31 espurios (áreas rojas/verdes enormes). Corregido
derivando `0x4000 | (kPlanes << 12)`.

## Medidas finales (A500_debug)

| Configuración | fps | ciclos/frame |
|---|---|---|
| Sync, sin blits | 49.92 | 142102 (= 1 campo) |
| Sync con blits | ~34 | 208667 |
| **Async con blits (bugs 2-4 corregidos)** | **~39-44** | 183832 (T161 en franja) |

La cadena async de 17 jobs cuesta ~42k ciclos de pared sobre el campo. Para 50 fps: reducir
lanzamientos/planos por job (rachas batch con posiciones alineadas, menos objetos) o abaratar la
programación por job (la caché de comunes, hoy desactivada por el bug 1 — solo segura con
control absoluto y sin servidores del SO que usen el Blitter).

## Segunda ronda: el crash del takeover y dos incoherencias silenciosas (cerrados)

Tras integrar `takeover_system()` la demo dejó de alcanzar READY. Diagnóstico con el MCP
(pila + desensamblado): PC `0xFFFFFF22` = `JSR.L (a6,-$00de)` con **`a6 = 0`**.

- **GfxBase duplicado (crash real)**: `takeover_system` guardaba `OpenLibrary("graphics.library")`
  en `g_sys.gfx`, pero las macros del SDK (`LoadView`/`WaitTOF`/`OwnBlitter`/`WaitBlit`) llaman
  por el **global `GfxBase`**, que quedaba a 0. Fuente única: `GfxBase` en todo el
  takeover/restore (`amiga.cpp`). Este crash era también la causa del «measure-fps devuelve
  frames=0» que se atribuyó a la resolución del run-status.
- **Unidades palabras/bytes (cazado por el sistema de coherencia)**: `sheet_advance` registraba
  las hojas del bloque de BOBs sumando el offset en **palabras** a una dirección en **bytes**;
  las regiones quedaban a mitad de camino y el mapa de regiones lo detectó (`0x7C001`, solape
  parcial orb/slider). Corregido con `off * 2` — el chequeo pagó su coste en su primera demo.
- **Cookie-cut planar sin dibujar (false silencioso)**: la demo declaraba `layout = Interleaved`
  en hojas **planar**; `bob_draw` devuelve `false` para cookie-cut `Interleaved` con máscara en
  plano suelto (`bob.hpp:399`) y el caminante/patrullero no se dibujaban (Or/Opaque ignoran ese
  campo: usan el del target). Corregido a `Planar`; `check_bob_plan` (capa 2) lo detecta en
  setup (`0x7C200`).

## Perfil del frame (para el presupuesto de 50 fps)

`g_eng_prof` (secciones del update, leídas por canal lateral) + `measure-fps`:

| Medida | Valor |
|---|---|
| Frame (async, copper parcheado) | 188 965 ciclos = 1.33 campos = **37.5 fps**; 19 jobs |
| Sección 0: espera de la cadena | 701 ciclos (la cadena ya terminó: sin espera explícita) |
| Sección 1: sim + plan + lanzamiento | 163 003 ciclos (86 %) — bisecada: updates 62k, **lanzamiento ~100k** |
| Sección 2: parcheo de armado + install | 338 ciclos (la re-emisión completa costaba ~10k) |

Lectura: con 19 jobs × 2 planos (target planar) la cadena entera (38 blits con espera entre
planos + 19 IRQs) se ejecuta **dentro del lanzamiento**: no hay solapamiento real CPU↔Blitter.
Palancas medidas para el siguiente paso: menos jobs (borrados → opacos con padding), abaratar
el coste por job (planos por job + IRQ), o lanzar la cadena antes (feeder incremental).

## Verificación

Visión (Ollama, secuencia de 3 frames): **7/7 puntos SÍ** (degradado, 6 zonas, orbes OR,
sliders fast, cookie-cut + save-under sobre damero, barras/joya, sprites HW), movimiento en
todas las zonas, sin artefactos. Diferencias entre frames: ~7.1-7.7k px.
