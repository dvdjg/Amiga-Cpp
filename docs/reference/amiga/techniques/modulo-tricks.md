# Modulo tricks (bitplanes)

- **Referencia tutorial:** [Modulo Tricks (powerprograms.nl)](https://www.powerprograms.nl/amiga/modulo-tricks.html)
- **Idea:** `BPL1MOD` y `BPL2MOD` alteran el incremento de puntero entre líneas de cada grupo de bitplanes; permite saltar memoria, ventanas, efectos de “skew” o límites de anchura sin recalcular todos los punteros a mano cada línea.
- **Coste:** Bajo en CPU si la lista copper o el setup por frame ya programa los registros; el DMA consume los mismos ciclos de lectura de bitplanes que el modo elegido.
- **Límites:** Debe cuadrar con `DIWSTRT`/`DIWSTOP`, `DDFSTRT`/`DDFSTOP` y número de planos; errores dan garbage o líneas corruptas. Dual playfield reparte planos entre campos pares/impares (ver AHRM Playfield).
- **AHRM:** Capítulo 3 Playfield; registros `BPL1MOD`, `BPL2MOD` — índice [amiga-hardware-manual-index.md](../amiga-hardware-manual-index.md).
- **Lab en repo:** Technique lab muestra valores actuales de `BPL1MOD`/`BPL2MOD` en overlay; amplía el efecto para animar módulos y validar en MCP con `winuae_custom_registers`.

## Cómo funciona el módulo

El hardware **ya avanza** los punteros `BPLxPT` por el número de bytes dibujados en la línea (`display_width/8`). `BPL1MOD`/`BPL2MOD` es lo que se **suma encima** de ese avance al pasar a la siguiente línea. De ahí salen todos los trucos: con módulo 0 el display normal; con módulo negativo se **retrocede** (repetir/mirror), con módulo positivo se **salta** (offset).

Fórmula base de la fuente (interleaved, con scroll horizontal ⇒ +2 bytes de la palabra extra de fetch):

| Efecto | Módulo (por plano) | Qué hace |
|---|---|---|
| **Stretch** | `-(display_width/8) - 2` | repite la línea actual hacia abajo (stretch infinito) |
| **Mirror** | `-((display_width/8) + 2 + (bitmap_width_bytes × planos))` | retrocede una línea completa ⇒ espejo vertical |
| **Repeat** | `-(bitmap_width_bytes × planos × n_lineas) - (display_width/8) - 2` | repite el bloque de arriba `n_lineas` más abajo |
| **Offset** | `+(bitmap_width_bytes × planos × n_lineas) - (display_width/8) - 2` | salta `n_lineas` del bitmap (display desplazado) |

Un efecto típico es: **WAIT a la línea de inicio → escribir módulo de efecto (x2: `$0108`/`$010a`) → WAIT a la línea siguiente → restaurar el módulo normal**. Se puede aplicar con `CopperIntent` (`BitplaneModulo` por franja, hoy pendiente) o con `Scheduler::move` directo.

## Efectos compuestos (de la fuente)

- **Water:** combina mirror + offset + repeat **por línea** (módulo calculado de una senoide, 32 líneas por frame) + **shift horizontal** por línea (`BPLCON1`) + **cambio de paleta** por línea (brillo/saturación). Los tres aspectos usan periodos distintos para que no se note la repetición. Valores precalculados en tablas (script Python) e iterados por frame.
- **TV (CRT off):** múltiples instancias del offset (21 actualizaciones de módulo por frame, 8 para el encogido vertical + resets) + paletas que funden a blanco + fase final de **fade horizontal con una sola entrada de color** reescrita por el Copper lo más rápido posible (píxeles de 8 px) usando `COPJMP2`/`COP2LC` para saltar entre sub-listas. Dura ~6 frames la fase vertical y ~8 la horizontal.
- **V-flip:** rota la pantalla sobre el centro en tiempo real **sin tocar la memoria de gráficos** (solo módulo).
- **Scroller (cracktro Turrican 2):** repite libremente partes de la pantalla vía módulo.

## Implementación en el engine

El vocabulario `graphics::CopperIntent` **no tiene** hoy un kind para el módulo (ver la nota de kinds pendientes en `engine/include/eng/graphics/raster_intent.hpp`). Mientras llega, el efecto se emite con `Scheduler::move(copper::Register::BPL1MOD/…)` dentro de una intención `PaletteLine` que comparta `top`, o directamente con `Scheduler::wait_line` + `move` en la parte estática del plan.

