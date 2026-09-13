# Escena retenida y modelo de recursos

Complementa la API pública (`PUBLIC_API.md`) con lo que hace que el engine sea manejable por el
desarrollador: una **escena persistente** (modo retenido) que es la fuente única de verdad de los
recursos, un **modelo de ocupación** para saber dónde hay hueco antes de saturar, **introspección**
de lo que hay por debajo, y **representación de actores** elegida por el engine (sprite/BOB/playfield).

## 1. Escena retenida

La aplicación no describe el frame entero cada vez: **modifica un estado persistente** (capas,
cámaras, actores, efectos) que el engine compila a display. La escena es explícita y enumerable:

```text
World                       (contenedor retenido; fuente única de verdad)
  ├─ Layer[]                playfield o efecto, con rol/profundidad y cámara
  ├─ Actor[]                con un Visual + una Representación (sprite/BOB/playfield/CPU)
  ├─ Camera[]               por capa o global (mundo ↔ pantalla)
  └─ Effect[]               raster/copper/blitter (ver VISUAL_EFFECT_SPRITE_DESIGN.md)
```

- Añadir/quitar elementos **actualiza el modelo de recursos** (§3). El engine no descubre nada por
  su cuenta en tiempo de frame.
- La composición de display (single/EHB/DPF/soft-DPF/HAM/chunky + `ModeSwitchZone`) se **deduce** de
  las capas/efectos; el desarrollador no la elige.

## 2. Representación de actores

El desarrollador describe **qué es** un actor (visual, tamaño, movimiento, prioridad, si es grande y
con scroll propio); el engine decide **cómo se materializa**, y puede cambiarlo si el presupuesto
cambia:

| Representación | Cuándo |
|---|---|
| Sprite hardware | cabe en un sprite, hay canal y presupuesto de DMA. |
| BOB (Blitter) | más grande que un sprite, o no hay canal. |
| **Playfield** (capa de un DPF) | objeto grande con scroll propio (patrón Jim Power). |
| CPU | casos pequeños/especiales (p. ej. un marcador). |

- La elección es **política del engine** (`RepresentationAllocator`), transparente a la app; puede
  **reasignar** (sprite→BOB) al añadir elementos, sin que el dev reescriba nada.
- El actor declara una **preferencia** (p. ej. `Visual`/`Layer`), no un mecanismo.

## 3. Modelo de recursos y ocupación

El engine lleva la **ocupación** de cada recurso del chipset y expone cuánto queda:

```text
Recurso                       Unidad            Uso
Chip RAM                      bytes             buffers/bitmaps/copperlist/patrones
DMA por línea                 slots             fetch de bitplanes + sprites + copper + audio
DMA / Blitter por frame       ciclos / palabras  cola de blits del frame
Slots de fetch de bitplanes   por línea          ancho de fetch y nº de planos activos
Copper                        movimientos/línea  cambios por línea (colores, punteros, modo)
Canales de sprite             8 (reutilizables)  ventanas por raster
Registros de paleta           32 (16+16)         capas + efectos de color
Playfields / planos           nº                 single/DPF/soft-DPF/HAM
Ventana de blanking           líneas             trabajo que debe caer fuera del visible
```

- API de consulta: `used(recurso)`, `available(recurso)`, `headroom(recurso)` y
  `can_add(descripción)` — el dev sabe **dónde hay hueco** antes de saturar.
- El **planner** valida en `init` y en cada `add_*`; si no cabe, **rechaza controladamente** con un
  diagnóstico, nunca degrada en silencio.
- En debug, **asserts por línea/frame** (peor caso) además del nominal.

## 4. Introspección (debug)

- `world.inspect()`: volcado estructurado de capas, representaciones elegidas, composición activa y
  ocupación usada/libre por recurso, a la consola de depuración.
- Overlay opcional: frame/peor-línea, DMA y ciclos de Blitter, canales de sprite, cabeceras de capa.
- Base existente: `FrameTelemetry`, contador de ciclos del periférico, `g_eng_run_status.detail`,
  checkpoints. La introspección es **solo de depuración**: la app la usa para entender, no para
  operar.

## 5. Composición en compile-time (sin código no usado)

- Las técnicas y capacidades son **tipos/policies** (templates/concepts): solo se instancia lo que
  la aplicación usa y el linker descarta el resto.
- Evitar `switch` en runtime sobre modos de display o tipos de efecto (instanciaría todos los
  caminos) y evitar virtuals: el modo/composición se resuelve en **compile-time** a partir del
  conjunto de features que la app referencia.
- La escena retenida es de **runtime**, pero cada elemento es de un **tipo de feature conocido en
  compile-time** y se guarda en buffers fijos por feature (sin heap, sin `std::variant` dinámico).

## 6. Playfield-as-actor (patrón Jim Power)

Un actor grande puede materializarse como **capa** (un playfield de un DPF) con su propia cámara y
scroll: es una representación más del `RepresentationAllocator`.

- El dev lo describe como actor con preferencia `Layer`; el engine decide si cabe (planos, DMA,
  fetch) y lo promociona a capa.
- Requiere modo con planos suficientes y, si comparte pantalla, un split/`ModeSwitchZone`; lo
  resuelve el planner.
- Si no cabe, el engine cae a la representación viable (BOB) o rechaza en `init`, según la política.

## 7. Relación con el resto

- API pública (la app no ve hardware): `PUBLIC_API.md`.
- Intenciones de efectos y schedulers: `VISUAL_EFFECT_SPRITE_DESIGN.md`.
- Modelo de playfields/scroll/composición: `PLAYFIELD_SCROLL_ARCHITECTURE.md`.
- Presupuestos por recurso y telemetría: `BACKGROUND_TASKS.md`, `docs/testing/`.
