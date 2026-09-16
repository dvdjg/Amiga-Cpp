# Composición de display: buffers y copper

Contrato de arquitectura para dos cosas que hoy están repartidas por el engine y que deben
tener **un único dueño a nivel de escena**: los **buffers de display** (simple/doble/triple)
y el **copper** (quién emite, en qué orden, qué se parchea y cuándo se publica).

## 1. El problema

El copper no es «un efecto visual»: en cuanto una escena lo usa de forma dinámica, pasa a
formar parte del **estado de render** por frame (`docs/engine/c-engine/engine-dynamic-copper-scene-notes.md`).
Hoy lo usan, cada uno por su cuenta:

- algoritmos de scroll (punteros `BPLxPT`, `BPLCON1`, módulos),
- bitmaps y objetos (paletas por zona para bobs/sprites/playfields),
- efectos de fondo (`copper sky`, degradados, reflejos),
- splits (`copper splits`, bandas y cambios de viewport),
- distorsión por scanline (offset de fetch/punteros línea a línea).

Y se emite desde cuatro sitios distintos que **reimplementan lo mismo** (doble buffer de
copperlist, parcheo por frame, alternancia de la lista activa): `TileScrollScene`,
`XlimitedDisplayComposer`, `XlimitedDualComposer` y arrays manuales en demos (`079`, `116`,
`082`, `083`). No hay un lugar único que decida **orden**, **prioridad**, **presupuesto** ni
**política de actualización**; y no existe la clase `DisplayComposition` que los documentos
objetivo dan por supuesta (`PLAYFIELD_SCROLL_ARCHITECTURE.md`).

## 2. Las tres granularidades (no confundirlas)

```
                     +-------------------------------+   quién
  display completo   | N buffers (planos+copperlist)  |   MultiBuffered<Driver, N>
                     | swap de COP1LC tras VBlank     |
                     +-------------------------------+
                                   ^  emite sobre un buffer
                     +-------------------------------+   Driver::bind()
  emisión de lista   | Scheduler / ListBuilder        |
                     +-------------------------------+
                                   ^  recibe intenciones ordenadas
                     +-------------------------------+   CopperPlan  (FALTA)
  orquestación       | tracks: sky, splits, paleta,  |
                     | scroll, HUD, sprites...        |
                     +-------------------------------+
  ---
  un solo plano     | PlaneView / SoftDpfComposition |   flip PARCIAL (un plano
                     | (dentro del mismo display)     |   del buffer, p.ej. DDAs)
  solo copperlist   | CopperDoubleBuffer             |   2 bloques de instrucciones
                     | (1 bitmap, 2 listas)           |   NO es doble buffer de bitmap
```

Las tres son ortogonales y **coexisten**: `TileScrollScene` (hoy) tiene doble buffer de
copperlist con bitmap único; `MultiBuffered` tiene doble buffer de display completo; el soft
DPF de la 112 tiene doble buffer de **un** plano.

## 3. Capa 1 — buffers de display: `MultiBuffered<Driver, N>`

Es la abstracción canónica de «cuántos buffers y cuándo se enseña cada uno». Propietaria de
la memoria (N parejas planos + copperlist), delega en el driver la **emisión** (`bind`) y en
el llamador la **decisión** de cuándo publicar.

```cpp
using Display = drivers::MultiBuffered<drivers::HamScene, 2>;   // 1 = sin DB, 3 = triple
Display display;
display.init(backend.memory(), cfg);   // reserva N buffers
display.takeover(backend);             // una vez: muestra el slot 0
// por frame:
paint(display.back());                 // back().bitplanes(), back().plane(i)
display.commit(backend);               // publica (tras VBlank o en la IRQ de blit)
```

Reglas:

- **N lo decide la escena, no el driver**: `N=1` (sin DB, con márgenes ocultos si hay scroll),
  `N=2` (efectos que reescriben lo visible; es el caso de 061/080/122), `N=3` solo para
  desacoplar un productor que tarda más de un campo (añade un frame de latencia).
- **El swap es solo `COP1LC`** y nunca lleva `COPJMP1` (`amiga_minimal.cpp:602-617`).
- **Quien posee memoria, posee el buffer**: una superficie de scroll o de efecto **no**
  reserva bitmaps ni decide el flip; escribe en el slot que le da el display.
- Excepción documentada: `PlaneView`/`SoftDpfComposition` hacen flip **de un plano** dentro
  del mismo display buffer (DDAs con fondo independiente). No se expresan como N buffers.

## 4. Capa 2 — emisión: `Scheduler` y `ListBuilder`

`eng::copper::ListBuilder` (`copper/copper.hpp:111`) emite palabras y devuelve **handles de
parcheo** (`move_at`, `move32`/`patch_move32`, `patch_data`, `instruction_address`).
`eng::copper::Scheduler` (`copper/scheduler.hpp:51`) ofrece la capa de intención
(`emit_planes_display`, `emit_palette`, `emit_palette_zone`, `emit_copper_intents`,
`emit_mode_switch_zone`, `wait_line_safe`) y un `ScheduleReport` con presupuesto de línea
(`heavy_palette_zones`, `timeline_spill`).

Estas dos son **herramientas**, no dueños: no deciden qué cambia por frame ni cuándo se
publica la lista.

## 5. Capa 3 — orquestación: `CopperPlan` (lo que falta)

El vocabulario portable **ya existe** (`graphics/raster_intent.hpp`: `VisualKind`,
`CopperIntent`, `CopperIntentKind`, `SpriteIntent`, y el patrón «un efecto aporta con
`apply_into(plan)`»), y `FramePlan` recoge trabajos por frame. Lo que falta es el
**supervisor por escena**:

```
  capas/efectos           CopperPlan (por buffer de display)         salida
  ---------------         ----------------------------------         ------
  sky      --\
  splits    --\  intents  1. recolecta y ORDENA por scanline         ListBuilder
  paleta     --+--------> 2. decide PATCH vs RE-EMISION              ->  words
  scroll     --/          3. lleva los handles de parcheo            ->  Scheduler
  sprites   --/           4. respeta el presupuesto de linea         ->  ScheduleReport
  HUD      --/            5. commit: publica con el swap de COP1LC   ->  install()
```

Contrato propuesto:

- **Un `CopperPlan` por buffer de display** (encaja con `MultiBuffered`): el plan se rellena
  en el buffer trasero y se publica al hacer `commit`. Sin doble buffer de copperlist no se
  puede parchear una lista viva con seguridad.
- **Los efectos no hablan de registros**: aportan `CopperIntent`/`SpriteIntent` (vocabulario
  portable); el plan los traduce a registros. Una heurística de calidad en PC puede consumir
  las mismas intenciones.
- **Política de actualización explícita por track**: `Reemit` (la lista cambia de forma) o
  `Patch` (solo words de valor, con handles). El plan expone cuántas words se parchean y el
  coste estimado; hoy esa decisión está duplicada y, en el caso XLimited, **ni siquiera se
  cumple** (el comentario promete parchear 13 words y `compose()` reemite la lista entera).
- **Orden por scanline y presupuesto**: los tracks se fusionan por línea de raster; el plan
  reporta spill (`timeline_over_budget_lines`) y zonas de paleta pesadas visibles antes de
  publicar, no después.
- **Un solo dueño de la lista activa**: mientras el Copper ejecuta la lista A, la CPU
  escribe la B; el plan es el único que llama a `install_copper_list`.

## 6. Reglas para desarrollos nuevos

1. Una escena = **1 composición de display** (`MultiBuffered` con su N) + **1 supervisor de
   copper** (`CopperPlan`); no se emite copper desde la lógica de juego ni desde un efecto.
2. Las superficies/capas **no poseen memoria de display** ni hacen flip: escriben en el
   buffer que les da el display.
3. Si un efecto necesita copper, implementa un **track** que aporta intenciones; declara
   `Patch`/`Reemit` y su coste.
4. Nada de `COPJMP1` fuera del arranque (`takeover`); el swap de frame es `COP1LC`.
5. Toda API nueva de display/copper se valida con **test host** (geometría de la lista,
   alternancia de buffer, handles de parcheo) además del gate visual de su demo.

## 7. Deuda actual (a normalizar)

| Duplicidad | Sitios | Fase |
|---|---|---|
| Doble buffer de copperlist | `tile_scroll.hpp:791`, `xlimited.hpp:1739/1928`, demos `079`/`116` | F1 |
| Doble buffer de display a mano | `082`, `083` (2 instancias), `079` (5), `116` (3) | F2 |
| Superficie de scroll con memoria propia | `double_buffer_playfield.hpp:100`, `flat_playfield.hpp`, `mirror_playfield.hpp` | F3 |
| Mapper de scroll duplicado | `amiga_display_mapper.hpp:52` vs `tile_scroll.hpp:670` | F5 |
| Política sin implementación | `virtual_scene.hpp:168/186` (`DoubleBufferedHiddenMargins`) | F0 |
| Doc↔código | `xlimited.hpp:1488-1491` (promete 13 words, reemite la lista) | F0 |
| Supervisión de copper por escena | **no existe** (`CopperPlan`) | F4 |
