# Arquitectura de Playfields y Scroll (modelo objetivo)

Modelo de referencia de dos conceptos que deben permanecer separados: el **algoritmo de scroll** y
el **playfield/superficie**. Define las capas, sus contratos y qué puede conocer cada una. El plan
de adopción por fases está en `docs/guides/roadmap/REFACTOR_PLAYFIELD_SCROLL.md`.

## 1. Principio

Separar **cuatro** responsabilidades que sólo deben colaborar a través de contratos estrechos:

```text
   ALGORITMO            SUPERFICIE               COMPOSICIÓN             MÁQUINA
   (cómo se mueve)      (dónde se dibuja)         (qué se ve)             (cómo se registra)
   ┌────────────┐       ┌──────────────┐          ┌──────────────┐        ┌──────────────┐
   │ Scroll     │ opera │ Playfield    │ produce  │ Display      │ mapea  │ Amiga        │
   │ Strategy   │──────▶│ (layout +    │────────▶ │ Composition  │───────▶│ platform     │
   │            │ sobre │  memoria)    │  View    │ (capas, fino │ a regs │ (copper/     │
   │            │       │              │          │  compartido) │        │  blitter)    │
   └────────────┘       └──────────────┘          └──────────────┘        └──────────────┘
```

Reglas de conocimiento:
- El **algoritmo** no conoce Amiga (ni BPLxPT, ni Copper, ni BG/FG). Sólo conoce una superficie de
  scroll (anillo + staging) y emite operaciones de scroll.
- El **playfield** posee memoria/geometría/layout y sabe dibujar bloques; no conoce la cámara ni el
  Copper.
- La **composición** conoce los roles (fondo, primer plano, HUD) y resuelve los **recursos
  compartidos por máquina** (en OCS: `BPLCON1` fino único por playfield, `BPLxMOD`, prioridad,
  split). Los acoplamientos fuertes entre capas viven aquí.
- La **plataforma** traduce una vista neutral a registros/Copper/Blitter.

## 2. Algoritmos de scroll (estrategias)

La familia de scroll se modela como **estrategias** aplicadas a una superficie, no como parte del
playfield. No usan virtuals (template/policy) para no romper el "sin virtuals en el hot path"
(`CODING_STYLE.md`).

```text
ScrollStrategy
  ├─ BigBufferScroll   superficie grande YA DIBUJADA; mover sólo el puntero de lectura.
  │                    Sin anillo, sin staging, sin draws por píxel. (Caso "escena completa".)
  └─ RingScroll        corkscrew (anillo + staging + split). Parametrizable por:
        ├─ AxisPolicy      X | Y | XY (8-way)
        └─ DirectionPolicy bi-direccional | un solo sentido
```

- Una estrategia **mantiene el estado de cámara** (`ScrollState`: mappos/videopos, dirección) y
  decide, para un desplazamiento `(dx,dy)`, la **operación de scroll** a emitir.
- Una estrategia **no dibuja**: delega en un contrato de emisión (ver §3.1).

### 2.1 Política de relleno y guarda (velocidad)

`RingScroll` se parametriza además por **cuánto trabajo por frame** y cuán ancha es la banda de
guarda, para soportar scroll rápido (varios tiles/frame) sin cambiar el núcleo:

```text
RingScroll<Axis, Direction, Fill, Guard>
  Fill  = Progressive | TileBurst<N> | StripPrerender<C>
  Guard = guard_tiles  (ancho/alto extra pre-pintado por delante de la ventana)
```

- `Progressive` = 1 px/sub-paso *paint-then-advance* (comportamiento actual).
- `TileBurst<N>` = pre-pinta N columnas/filas completas en la guarda y avanza en fronteras de
  tile; la dirección se lacea a frontera (menos casos de costura).
- `StripPrerender<C>` = mantiene C columnas/filas ya dibujadas y solo mueve punteros.

Invariante: `guard_tiles >= ceil(max_px_por_frame / tile) + 1`. Detalle, regímenes, tear-free y
presupuesto en `FAST_SCROLL.md`.

Selección estática implementada: `eng::field::ScrollProfile`/`ScrollProgressive`/`ScrollFastN`
(`engine/include/eng/field/scroll_profile.hpp`) se pasa como parámetro de tipo a
`XLimitedPlayfield`/`XlimitedScene` (default `ScrollProgressive` = comportamiento clásico). Incluye
paso por frame, guarda X/Y, avance por tiles completos (dirección laceda a frontera) y staging
vertical por perfil.

## 3. Playfields (superficies)

`Playfield<N>` es la base: memoria + geometría + mapeo lógico→físico + primitivas (`write_pixel`,
`fill_rect`, `draw_line`) y producción de la **vista** que consume la composición. Se parametriza
por su **política de layout** y por su **estrategia de scroll** (opcional), no por nuevas clases.

```text
Playfield<N>            superficie con bitmap propio o vista, N=1..6 planos (incluye EHB 6).
  ├─ Layout = Flat      mapeo contiguo (fila = wy*planes), sin walk, fetch estándar (DDF $38).
  └─ Layout = Ring      mapeo del corkscrew/anillo (staging + walk + espejo opcional), DDF $30.
  + (opcional) ScrollStrategy: None | BigBuffer | Ring(X/Y/XY).
```

Casos de uso:
- **Superficie estática** (HUD, panel, capa de actores, fondo estático): `Playfield<N>` con
  `Layout = Flat` y sin estrategia de scroll.
- **Campo con scroll** (single de un juego, FG o BG de un DPF): `Playfield<N>` con `Layout = Ring`
  y `RingScroll`.
- **Escena ya dibujada**: `Playfield<N>` con `Layout = Flat` y `BigBufferScroll`.
- **Modos de 1..6 planos / EHB**: parámetro `N`; el modo EHB lo fija la composición.
- **FG/BG de DPF**: dos `Playfield<N>` con bitmaps propios y rol de profundidad (composición).
- **Soft DPF (RoboCod)**: un `Playfield<4>` (FG) + un **`PlaneView`** (vista del plano 5 del
  bitmap del FG, 1 bit) con su propio doble buffer; ver §3.2.

### 3.1 Contrato algoritmo ↔ superficie

Se separa el "cómo se mueve" del "qué se dibuja":

```text
ScrollTarget   (layout, sin dibujar):  geometría del anillo/bloques, límites del mapa, wrap.
ScrollEmitter  (dibujar):              pinta la banda entrante en la posición de anillo dada
                                       y gestiona la costura (saveword) del layout.
```

`ScrollStrategy::step(plan, target, emitter, dx, dy)` decide **una** operación y se la pide al
emitter. Así el algoritmo es portable (Mega Drive, SNES…) y testeable con un mock
(`tests/host/023_limited_axes`).

Implementado como **conceptos** en `engine/include/eng/field/scroll_engine.hpp`:
`ScrollTarget` (geometría, sin dibujar) + `ScrollEmitter` (`add_draw` + `save_word`/`restore_saveword`)
= `ScrollSink`; `XLimitedPlayfield` cumple las tres (HOST-036).

### 3.2 Vista de planos (`PlaneView`)

Un `PlaneView` es una superficie que **no posee memoria**: referencia N planos de otro
`Playfield` con el mismo layout/stride. Habilita:

- el **BG de un soft DPF** (vista de 1 plano del bitmap del FG) con doble buffer propio;
- sub-campos de un DPF que comparten modulos;
- cualquier capa que reutilice el bitmap de otra sin duplicarlo.

`Playfield<N>` puede poseer memoria o ser una vista; la composición decide a qué bitmap apunta.

Implementado como `eng::field::PlaneView` (`engine/include/eng/field/plane_view.hpp`): vista del
plano de fondo con **doble buffer opcional** (`bind_single`/`enable_double_buffer`, `display_base`,
`write_base`, `flip`). Sobre él, `eng::field::SoftDpfComposition` (`soft_dpf.hpp`) es la composición
completa: geometría + vista + construcción de los blits de copia del patrón; `XLimitedPlayfield`
solo la configura y delega (`bg_flip`/`make_bg_plane_copy_*`/`fill_parallax_pattern`). Tests
HOST-038/039; demo 112 sin regresión (`K_DIAG_YONLY`+`K_DIAG_BG_FIXED`: borde de fondo constante).

## 4. Composición (DisplayComposition)

Contiene la lista de **capas** (cada una un `Playfield` + su estrategia o ninguna), su rol y
profundidad, y **resuelve los recursos compartidos de la máquina**:

```text
DisplayComposition
  ├─ SingleComposition<N>    un playfield de N planos.
  ├─ EhbComposition          single 6 planos en modo EHB.
  ├─ DpfComposition          PF1 + PF2 (bitmaps propios). Fino independiente por nibble de BPLCON1.
  └─ SoftDpfComposition      FG(N planos) + BG(1 plano vista) que comparten bitmap y BPLCON1.
        │
        └─ resuelve: BPLCON1 (fino), BPLxMOD, BPLCON2 (prioridad), split, paleta, sprites,
                     y zonas de conmutación de modo (ModeSwitchZone).
```

Recursos compartidos:
- El **fino** (OCS: `BPLCON1` es común a todos los planos de un playfield) lo fija la composición.
  En `SoftDpfComposition` FG y BG comparten fino (normalmente lo dicta el FG y el BG compensa su
  coarse). En `DpfComposition` cada campo tiene su nibble de fino.
- La composición decide de qué buffer se lee cada plano (p. ej. el plano de fondo del soft DPF se
  lee de su buffer delantero mientras el resto se lee del bitmap principal).

### 4.1 Zonas de conmutación de modo (`ModeSwitchZone`)

La composición puede partir la pantalla en **tramos con geometría de vídeo distinta** (no sólo
punteros): un tramo con más planos para el juego y otro con menos para el HUD/marcadores. En el
`WAIT` de la línea de corte se reprograma, en este orden, `BPLCON0` (BPU), `DDFSTRT/DDFSTOP`,
`BPL1MOD/BPL2MOD` y después los `BPLxPT` del tramo, con el `DDF` alineado y word de guarda.

- Permite que el HUD sea un `Playfield<N>` con menos planos sin arrastrar la geometría del campo,
  reservando sólo su memoria.
- Una **variante** de la zona conmuta sólo punteros/paleta manteniendo la geometría del campo.
- La conmutación de geometría **requiere microtests** (HUD de 2/3/4 planos bajo un split sobre un
  campo de 4/5/6) porque la primera línea del tramo toma el puntero nuevo pero puede conservar la
  geometría de fetch anterior hasta que el pipeline se recarga. El API es
  `eng::graphics::ModeSwitchZone` + `Scheduler::emit_mode_switch_zone` (HOST-042); la demo
  `113_mode_switch` verifica el caso 5→2 planos (MI09).

### 4.2 Modos de display

El modo de display es una propiedad de la composición; puede ocupar toda la pantalla o un tramo
(`ModeSwitchZone`). No cambia el modelo de las superficies ni de las estrategias de scroll.

```text
Planar     SingleComposition<N>   1..6 planos (EHB = 6 con bit de half-brite).
           DpfComposition         PF1 + PF2, bitmaps propios, fino por nibble.
           SoftDpfComposition     FG(N) + BG(1 vista) sobre un bitmap, fino compartido.
HAMDisp    HamComposition         6 planos en modo HAM; el Copper cambia COLORxx por línea.
Chunky     CopperChunkyComposition color/registros por línea sin framebuffer planar; lista densa.
```

- **HAM** y **copper chunky** no son `Playfield<N>`: son **modos de la composición** que el Copper
  materializa con cambios por línea. Un `Effect` aporta esas líneas (ver §4.3).
- Un modo puede **convivir** con otro en la misma pantalla (p. ej. juego planar arriba, franja de
  copper chunky o HUD HAM abajo) mediante `ModeSwitchZone`.
- El **cuadruplicado de líneas** del HAM (mismo par de planos leído 4 veces con `BPLxMOD`/`BPLCON1`
  alternos cada 4 líneas) es una lista de Copper generada, no una superficie nueva.

### 4.3 Capas efectistas (Copper / Blitter / Sprites)

Además de las capas `Playfield`, la composición acepta **capas efectistas** que emiten
**intenciones** sobre ventanas de líneas raster. El vocabulario y los schedulers están en
`VISUAL_EFFECT_SPRITE_DESIGN.md` (`CopperIntent`, `BlitIntent`, `SpriteIntent`, `PaletteIntent` →
`FramePlan`); aquí sólo se fija cómo encajan con playfields y modos:

- **Copper chunky / degradados / raster colors**: capa de `CopperIntent` (WAIT+MOVE por línea) que
  modifica `COLORxx` u otros registros; puede cubrir todo el playfield o una franja. Ver
  `copper-chunky.md` y la zona de color del compositor single.
- **HAM por líneas**: capa que emite `PaletteIntent`/cambios de registros por línea (y, si aplica,
  el offset de `BPL1MOD`/`BPLCON1` del cuadruplicado). Ver `FIRE_RGB_PORT_PLAN.md`.
- **Reflejos y efectos de Blitter**: capa de `BlitIntent` (copia espejada, máscaras, áreas) que
  reescribe regiones de un bitmap; combinable con cambios de Copper por línea.
- **Sprites avanzados**: capa de `SpriteIntent` resuelta por el `SpriteAllocator` (multiplexado,
  reutilización de canales por raster). Ver `sprite-layer.md`.
- **Conversión CPU→planar**: el C2P por Blitter (`C2P_BLITTER.md`) es el puente entre una capa
  efectista que escribe **chunky** (CPU) y el display **planar**; puede hacerse síncrono o por
  interrupción de blit.

Regla: el `Effect` no ve registros ni punteros; emite intenciones con su ventana de líneas. El
scheduler dueño del recurso (Copper/Blitter/Sprites) las compila al `FramePlan` y resuelve
conflictos (presupuesto de DMA, dueño único, orden vertical).

## 5. Mapping de máquina (Amiga)

El playfield/algoritmo exponen una **vista neutral** (`ScrollView`: cámara, viewport, geometría
del anillo, layout interleaved, doble buffer del fondo). Un **`AmigaDisplayMapper`** la traduce a
`planeaddx`, `BPLCON1`, `BPLxMOD`, punteros y línea de split. Ni el playfield ni la estrategia
calculan registros.

- El **corkscrew/split** y los modos de fetch (`BPL32/BPAGEM`) son del mapper Amiga.
- El **doble buffer del plano de fondo** (soft DPF) pertenece a la superficie/composición; el
  mapper sólo conmuta el puntero del plano de fondo.

## 6. Restricciones de diseño

- Sin heap, sin RTTI, sin virtuals en el hot path, `gnu++23` (`CODING_STYLE.md`).
- Geometría del hot path como constante NTTP + `fast_div` (potencias de dos → shifts; constante
  general → multiplicación mágica); sin `__udivsi3`/`__umodsi3`.
- Los invariantes del anillo (dimensionado de `display_height`, wrap de la banda de staging, bias
  visible) son del algoritmo y no cambian; ver `XYLIMITED_ALGORITMO_GENERICO.md` §1.2.
- El contrato del backend y los presupuestos de Blitter se preservan.

## 7. Referencias

- Algoritmo y vocabulario (invariantes del anillo): `XYLIMITED_ALGORITMO_GENERICO.md`.
- Modelo circular vs interleaved y saveword: `CIRCULAR_VS_XLIMITED.md`.
- DPF: Y por campo, split, lineal, mixto: `DPF_MIXTO_SPLIT_LINEAL.md`.
- Efectos de Copper/Blitter/Sprites (intenciones y schedulers): `VISUAL_EFFECT_SPRITE_DESIGN.md`.
- Copper chunky: `docs/reference/amiga/techniques/copper-chunky.md`; C2P por Blitter: `C2P_BLITTER.md`.
- HAM del fuego (cuadruplicado + por-línea): `docs/demos/effects/FIRE_RGB_PORT_PLAN.md`.
- Técnica RoboCod (soft DPF): `docs/reference/amiga/techniques/robocod-layered-scroll.md`.
- Drivers gráficos por estrategia de composición: `GRAPHICS_DRIVERS.md`.
- API pública (la aplicación no ve hardware): `PUBLIC_API.md`.
- Plan de adopción: `docs/guides/roadmap/REFACTOR_PLAYFIELD_SCROLL.md`.
