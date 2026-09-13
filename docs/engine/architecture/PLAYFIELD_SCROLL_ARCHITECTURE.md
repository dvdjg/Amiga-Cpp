# Arquitectura de Playfields y Scroll (modelo objetivo)

Este documento fija el **modelo objetivo** de dos conceptos que hoy están fusionados en
`engine/include/eng/field/xlimited.hpp`: el **algoritmo de scroll** y el **playfield/superficie**.
Describe las capas, sus contratos y qué puede saber cada una. Es el contrato de refactor que deben
seguir las demos y la documentación; el plan por fases está en
`docs/guides/roadmap/REFACTOR_PLAYFIELD_SCROLL.md`.

> Estado: **propuesta/origen**. El código actual implementa una versión funcional pero acoplada
> (`XLimitedPlayfield` acumula superficie + algoritmo + roles BG/FG + mapping Amiga). La crítica
> del estado actual está en `XYLIMITED_ALGORITMO_GENERICO.md` §3; este documento es su sucesor
> canónico para el modelo objetivo.

## 1. Principio

Separar **cuatro** responsabilidades que no deben conocerse entre sí más allá de contratos
estrechos:

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
  split). Es el único sitio donde viven los acoplamientos "fuertes" tipo RoboCod.
- La **plataforma** traduce una vista neutral a registros/Copper/Blitter.

## 2. Algoritmos de scroll (estrategias)

La familia de scroll de Steger se modela como **estrategias** aplicadas a una superficie, no como
parte del playfield. No usan virtuals (template/policy) para no romper el "sin virtuals en el hot
path" (`CODING_STYLE.md`).

```text
ScrollStrategy
  ├─ BigBufferScroll   superficie grande YA DIBUJADA; mover sólo el puntero de lectura.
  │                    Sin anillo, sin staging, sin draws por píxel. (Caso "escena completa".)
  └─ RingScroll        corkscrew (anillo + staging + split). Parametrizable por:
        ├─ AxisPolicy      X (XLimited) | Y (YLimited) | XY (XYLimited, 8-way)
        └─ DirectionPolicy bi-direccional | un solo sentido (optimizaciones)
```

- Una estrategia **mantiene el estado de cámara** (`ScrollState`: mappos/videopos, dirección) y
  decide, para un desplazamiento `(dx,dy)`, la **operación de scroll** a emitir.
- Una estrategia **no dibuja**: llama a un contrato de emisión (ver §3.1).
- El "buffer grande ya dibujado" y el anillo comparten el estado de cámara, pero el primero no
  tiene guardas/anillo; es una estrategia trivial y separada.

## 3. Playfields (superficies)

`Playfield` es la base: memoria + geometría + mapeo lógico→físico + primitivas (`write_pixel`,
`fill_rect`, `draw_line`) y producción de la **vista** que consume la composición. **Es ÚNICA por
tipo de memoria/layout**; lo que varía es su **política de layout** y si tiene **estrategia de
scroll**, no su identidad de clase.

```text
Playfield<N>            superficie con bitmap PROPIO (o vista), N=1..6 planos (incluye EHB 6).
  ├─ Layout = Flat      mapeo contiguo (fila = wy*planes), sin walk, fetch estándar (DDF $38).
  │                     => HUD, panel, capa de actores, fondo estático.
  └─ Layout = Ring      mapeo del corkscrew/anillo (staging + walk + espejo opcional), DDF $30.
                        => campo con scroll (FG de un juego, BG de un DPF, single de un juego).
  + (opcional) ScrollStrategy: None | BigBuffer | Ring(X/Y/XY).
```

- **`CanvasPlayfield` desaparece como clase**: es `Playfield<N>` con `Layout = Flat` y
  `ScrollStrategy = None`. No hay nada que lo distinga salvo esas dos políticas.
- **Modos de 1..6 planos / EHB**: parámetro `N` (y el modo EHB lo fija la composición). Nunca una
  clase nueva.
- **FG/BG de DPF**: dos `Playfield<N>` con bitmaps propios y rol de profundidad (composición).
- **Soft DPF (RoboCod)**: un `Playfield<4>` (FG) + un **`PlaneView`** (vista del plano 5 del
  bitmap del FG, 1 bit) con su propio doble buffer; ver §3.2.
- **BigBuffer (escena ya dibujada)**: `Playfield<N>` con `Layout = Flat` y estrategia
  `BigBufferScroll` (sólo mueve el puntero).

### 3.1 Contrato algoritmo ↔ superficie

Se separa el "cómo se mueve" del "qué se dibuja":

```text
ScrollTarget   (layout, sin dibujar):  geometría del anillo/bloques, límites del mapa, wrap.
ScrollEmitter  (dibujar):              pinta la banda entrante en la posición de anillo dada
                                       y gestiona la costura (saveword) del layout.
```

`ScrollStrategy::step(plan, target, emitter, dx, dy)` decide **una** operación y se la pide al
emitter. Así el algoritmo es portable (Mega Drive, SNES…) y testeable con un mock
(`tests/host/023_limited_axes` ya lo hace con `ScrollEngine`/`ScrollSink`; el refactor sólo
oficializa la separación target/emitter).

### 3.2 Vista de planos (`PlaneView`)

Un `PlaneView` es una superficie que **no posee memoria**: referencia N planos de otro
`Playfield` con el mismo layout/stride. Habilita:

- el **BG de un soft DPF** (vista de 1 plano del bitmap del FG) con doble buffer propio;
- sub-campos de un DPF que comparten modulos;
- y, en general, cualquier “capa” que reutilice el bitmap de otra sin duplicarlo.

`Playfield<N>` puede poseer memoria o ser una vista; la composición decide a qué bitmap apunta.

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

### 4.1 Zonas de conmutación de modo (`ModeSwitchZone`)

La composición puede partir la pantalla en **tramos con geometría de vídeo distinta** (no sólo
punteros): un tramo con más planos para el juego y otro con menos para el HUD/marcadores. En el
`WAIT` de la línea de corte se reprograma, **en este orden**, `BPLCON0` (BPU), `DDFSTRT/DDFSTOP`,
`BPL1MOD/BPL2MOD` y después los `BPLxPT` del tramo (con el `DDF` alineado y word de guarda).

- Esto es lo que hace posible que **el HUD sea un `Playfield<N>` con menos planos** sin arrastrar la
  geometría del campo, reservando sólo su memoria.
- La versión conservadora que hoy usa `XlimitedDisplayComposer` (misma geometría y sólo conmutar
  punteros/paleta) sigue siendo válida y se ofrece como modo “misma geometría” de la zona.
- La conmutación de geometría **debe validarse con microtests** (HUD de 2/3/4 planos bajo un split
  sobre un campo de 4/5/6): el pitfall conocido es el orden de los MOVE (la primera línea toma el
  puntero nuevo pero la geometría vieja y lee memoria contigua). No es una limitación del chipset.


- El **fino compartido** (OCS: `BPLCON1` es común a todos los planos de un playfield) es una
  restricción que vive **aquí**. En `SoftDpfComposition`, FG y BG comparten fino: la composición
  lo fija (normalmente lo dicta el FG) y el BG compensa su coarse. En `DpfComposition` cada campo
  tiene su nibble de fino.
- La composición es la que decide, por ejemplo, que en el soft DPF el plano de fondo se lee de
  `bg_plane_base` (doble buffer) mientras el resto se lee del bitmap principal.

## 5. Mapping de máquina (Amiga)

El playfield/algoritmo exponen una **vista neutral** (`ScrollView`: cámara, viewport, geometría
del anillo, layout interleaved, doble buffer del fondo). Un **`AmigaDisplayMapper`** la traduce a
`planeaddx`, `BPLCON1`, `BPLxMOD`, punteros y línea de split. Ni el playfield ni la estrategia
calculan registros.

- El **corkscrew/split** y los modos de fetch (`BPL32/BPAGEM`) son del mapper Amiga.
- El **doble buffer del plano de fondo** (soft DPF) es de la composición/superficie (buffers), y
  el mapper sólo conmuta el puntero del plano de fondo.

## 6. Mapa de migración (código actual → objetivo)

| Actual | Objetivo |
|---|---|
| `Playfield` (base) | `Playfield<N>` (base unificada: memoria/vista + layout) |
| `XLimitedPlayfield<N>` | `Playfield<N>` (Layout=Ring) + `RingScroll` (estrategia) + `RingLayout`/`Emitter` |
| `ScrollEngine`/`ScrollSink` | `RingScroll`/`BigBufferScroll` + `ScrollTarget`/`ScrollEmitter` |
| `ScrollMode`/`AxisMode` | `AxisPolicy` + `DirectionPolicy` |
| `CanvasPlayfield` | `Playfield<N>` (Layout=Flat, ScrollStrategy=None) — **misma clase** |
| (hoy `parallax_plane`+`bg_flip`+`make_bg_plane_copy_*` en `XLimitedPlayfield`) | `PlaneView<1>` + `SoftDpfComposition` (doble buffer) |
| `XlimitedScene` (m_field[2], `bg()/fg()`, `hud`) | `DisplayComposition` con lista de capas con rol (+ `ModeSwitchZone` para HUD) |
| `XlimitedDisplayComposer`/`XlimitedDualComposer` | `SingleComposition`/`EhbComposition`/`DpfComposition`/`SoftDpfComposition` (+ `ModeSwitchZone`) |
| `hardware_view` (planeaddx/bplcon1/bplmod) | `ScrollView` neutral + `AmigaDisplayMapper` |
| `XlimitedPathConfig` (`update_auto`/`effect`) | driver de la **demo** (`TourDriver`) |

## 7. Estado actual vs objetivo

- **Algoritmo ya separado**: `ScrollEngine` + `concept ScrollSink` existen y son reutilizables;
  falta oficializar `ScrollTarget`/`ScrollEmitter` y las estrategias nombradas.
- **El acoplamiento grande** está en: (a) el soft DPF dentro de `XLimitedPlayfield`; (b) el mapping
  Amiga dentro de `hardware_view`; (c) roles FG/BG/HUD dentro de `XlimitedScene`; (d) flags de
  variante (`ScrollMode`/`AxisMode`).
- **Lo que no se toca**: el comportamiento del algoritmo (invariantes del anillo, §1.2 de
  `XYLIMITED_ALGORITMO_GENERICO.md`), el contrato del backend, los presupuestos de Blitter y las
  restricciones de `CODING_STYLE.md`.

## 8. Referencias

- Crítica del estado actual y vocabulario del algoritmo: `XYLIMITED_ALGORITMO_GENERICO.md`.
- Modelo circular vs interleaved y saveword: `CIRCULAR_VS_XLIMITED.md`.
- DPF: Y por campo, split, lineal, mixto: `DPF_MIXTO_SPLIT_LINEAL.md`.
- Técnica RoboCod (soft DPF): `docs/reference/amiga/techniques/robocod-layered-scroll.md`.
- Drivers gráficos por estrategia de composición: `GRAPHICS_DRIVERS.md`.
- Roadmap por fases: `docs/guides/roadmap/REFACTOR_PLAYFIELD_SCROLL.md`.
