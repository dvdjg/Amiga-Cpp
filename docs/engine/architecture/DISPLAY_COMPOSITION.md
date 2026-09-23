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
  display completo   | N buffers (planos+copperlist)  |   scene::compose (buffers=N)
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
copperlist con bitmap único; `scene::compose` con `buffers>1` tiene doble buffer de display completo; el soft
DPF de la 112 tiene doble buffer de **un** plano.

## 3. Capa 1 — buffers de display: `scene::compose` (`SceneResources.buffers`)

Es la abstracción canónica de «cuántos buffers y cuándo se enseña cada uno». La posee la
**escena**: `SceneResources.buffers` (1/2/3) reserva N bitmaps y la etapa `display` emite los
`BPLxPT` como MOVEs parcheables; `Scene::commit()` repunta los punteros al buffer trasero. El
doble buffer **de display** es eso; el doble buffer **de copperlist** es `copper::DoubleBuffer`
(lo orquesta el `Plan`). (El antiguo `drivers::MultiBuffered<Driver, N>` está **retirado**; el
modo `SceneMode::CopperChunky` cubre el caso sin bitplanes.)

```cpp
scene::SceneResources res = scene::planar(320, 256, 4);
res.buffers = 2;                       // 1 = sin DB, 3 = triple
scene::compose(scene, memory, res, limits, /* etapas */);
scene.commit();                        // publica el buffer trasero (parchea BPLxPT)
```

Reglas:

- **N lo decide la escena, no el driver**: `N=1` (sin DB, con márgenes ocultos si hay scroll),
  `N=2` (efectos que reescriben lo visible; es el caso de 061/080/122), `N=3` solo para
  desacoplar un productor que tarda más de un campo (añade un frame de latencia).
- **El swap es solo `COP1LC`** y nunca lleva `COPJMP1` (`amiga.cpp:602-617`).
- **Quien posee memoria, posee el buffer**: una superficie de scroll o de efecto **no**
  reserva bitmaps ni decide el flip; escribe en el buffer trasero de la escena.
- Excepción documentada: `PlaneView`/`SoftDpfComposition` hacen flip **de un plano** dentro
  del mismo display buffer (DDAs con fondo independiente). No se expresan como N buffers.
- **Modo sin bitplanes** (`SceneMode::CopperChunky`, `planes == 0`): la escena solo reserva
  copperlist; la lista la emite `composition::CopperChunkyLayer` y se publica con
  `Scene::present(backend)`.

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

Contrato propuesto (API concreta; primer consumidor previsto: `055_copper_rainbow`, que
hoy emite la lista entera cada frame en un único bloque):

```cpp
namespace eng::copper {

/// Plan de copper de una escena: recolecta las intenciones de las capas/efectos
/// ("tracks"), las ORDENA por scanline y las materializa en la copperlist del buffer
/// TRASERO de un `DoubleBuffer`, que se publica con el swap de COP1LC.
///
/// Es el único dueño de la lista activa: los efectos no emiten copper, aportan
/// `graphics::CopperIntent` (vocabulario portable de `raster_intent.hpp`).
class Plan {
public:
    static constexpr u8 max_intents = 64;

    bool begin(eng::MemorySystem& memory, const PlanConfig& cfg); // reserva el DoubleBuffer

    /// Parte estática (display, módulos, paleta base): se emite UNA vez por frame en el
    /// bloque trasero. `emit` recibe un `Scheduler` ya situado en el trasero, de modo que
    /// el llamador no elige bloque ni toca COP1LC.
    template <class EmitFn> void emit_static(EmitFn emit);

    void add(const graphics::CopperIntent& it);          // intención suelta
    void add(const graphics::CopperIntent* its, u8 n);   // lote de una capa/efecto

    /// Materializa: ordena por `top`, emite las intenciones sobre lo estático, voltea el
    /// DoubleBuffer y (opcionalmente) publica. Devuelve false si no cupo o hubo overflow.
    bool commit();

    template <class Backend> void takeover(Backend&) const; // una vez
    template <class Backend> void install(Backend&) const;  // swap de COP1LC

    constexpr u16 words() const;                          // tamaño de la lista emitida
    constexpr const ScheduleReport& report() const;       // presupuesto por línea
};

} // namespace eng::copper
```

Qué añade sobre lo que ya hay: **el orden por scanline deja de ser una obligación del
llamador** (`emit_copper_intents` exige intenciones en orden ascendente de línea: hoy es un
invariante implícito en cada demo), la emisión vive en el bloque trasero (con COP1LC swap,
sin tocar la lista activa) y el presupuesto (`ScheduleReport`, `timeline_over_budget_lines`)
se consulta antes de publicar. La política `Patch` vs `Reemit` se declara por track: los
registros con handle (`Scheduler::move_at`) se parchean; la estructura se reemite.

## 6. Reglas para desarrollos nuevos

1. Una escena = **1 composición de display** (`scene::compose` con `buffers=N`) + **1 supervisor de
   copper** (`CopperPlan`); no se emite copper desde la lógica de juego ni desde un efecto.
2. Las superficies/capas **no poseen memoria de display** ni hacen flip: escriben en el
   buffer que les da el display.
3. Si un efecto necesita copper, implementa un **track** que aporta intenciones; declara
   `Patch`/`Reemit` y su coste.
4. Nada de `COPJMP1` fuera del arranque (`takeover`); el swap de frame es `COP1LC`.
5. Toda API nueva de display/copper se valida con **test host** (geometría de la lista,
   alternancia de buffer, handles de parcheo) además del gate visual de su demo.
6. El copper se orquesta con `eng::copper::Plan` (`engine/include/eng/graphics/copper/plan.hpp`),
   no emitiendo a mano. Los efectos/capas **no hablan de registros**: aportan `graphics::CopperIntent` (vocabulario portable de `raster_intent.hpp`) al plan, que **ordena por scanline relativo al inicio del display** (el listado envuelve a 256 líneas), lo materializa en el bloque **trasero** de su doble buffer de copperlist y publica con el swap de `COP1LC` (`Plan::commit`).
7. **No llamar `install_copper_list`/`takeover_display` a mano** en código nuevo: usar `Plan::commit`/`Plan::takeover` (o `Scene::present`/`takeover` en el modo copper-chunky).
8. **Dos granularidades distintas, no confundirlas**: *buffers de display* (bitmaps) → `scene::compose` (`SceneResources.buffers` = 1/2/3); *buffers de copperlist* → `copper::DoubleBuffer` (lo que usa el `Plan`; `attach()` permite orquestar uno externo).
9. Un display sin bitplanes usa el modo `SceneMode::CopperChunky` (`planes == 0`); la escena no reserva planos.
10. **Nº de buffers de display frente a la carga del frame (anti-tearing).** El swap de bitmap es `COP1LC` y solo surte efecto **al comienzo del siguiente VBlank**. Con un `update` que **arranca alineado a VBlank** y dura `W` campos, **2 buffers** bastan sin tearing: se dibuja el buffer que acaba de salir de pantalla (patrón de `effects/bobs3d`: clear/draw + `TaskWaitVBlank` + swap). Si `W` **no** esta alineado (p. ej. `Engine::run_frames` relanza `update` a media pantalla cuando `W > 1` campo), con 2 buffers el dibujo del destino empieza antes del swap y hay **tearing**; entonces hacen falta **`ceil(W/campo) + 1` buffers** (habitualmente 3 = triple buffer), que desacoplan dibujo y visualización. Regla practica: si el efecto no cabe en 1 campo, `run_frames_polling` + 2 buffers es lo mas simple y sin tearing; usar 3 buffers (`SceneResources.buffers = 3`) cuando se necesite no alinear. Caso medido y documentado: **demo 117_bobs3d** (`docs/demos/effects/BOBS3D_PORT_PLAN.md` §6 y §9.3).

Ejemplo vivo: **demo 085 `copper_plan_scene`** (cielo por bandas de la escena + BOB con degradado anclado a su Y, dos fuentes que el plan ordena por scanline).

## 7. Deuda actual (a normalizar)

| Duplicidad | Sitios | Fase |
|---|---|---|
| Doble buffer de copperlist | `tile_scroll.hpp:791`, `xlimited.hpp:1739/1928`, demos `079`/`116` | F1 |
| Doble buffer de display a mano | `079` (5), `116` (3) | F2 |
| Superficie de scroll con memoria propia | `double_buffer_playfield.hpp:100`, `flat_playfield.hpp`, `mirror_playfield.hpp` | F3 |
| Mapper de scroll duplicado | `amiga_display_mapper.hpp:52` vs `tile_scroll.hpp:670` | F5 |
| Política sin implementación | `virtual_scene.hpp:168/186` (`DoubleBufferedHiddenMargins`) | F0 |
| Doc↔código | `xlimited.hpp:1488-1491` (promete 13 words, reemite la lista) | F0 |
| Supervisión de copper por escena | **no existe** (`CopperPlan`) | F4 |
| **Eje Driver ↔ Field/Surface** (resuelto): los drivers legacy exponían `bitplanes()` crudos sin `Surface`, y los `Playfield` no encajaban con un doble buffer de display. Normalización **hecha**: `scene::compose` con `buffers > 1` da el doble/triple buffer sobre `CanvasPlayfield`/`ContiguousPlayfield` (HOST-212); los drivers `CanvasScene`/`CopperChunkyScene` y `MultiBuffered` están **retirados** (el display sin bitplanes es `SceneMode::CopperChunky`). `XLimitedPlayfield` (scroll, memoria propia) no ofrece `bind()`; no hay doble buffer de scroll sobre él. | F2 |
