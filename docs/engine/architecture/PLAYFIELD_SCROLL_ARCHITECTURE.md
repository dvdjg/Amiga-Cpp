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

### 3.2 Vista de planos (`PlaneView`)

Un `PlaneView` es una superficie que **no posee memoria**: referencia N planos de otro
`Playfield` con el mismo layout/stride. Habilita:

- el **BG de un soft DPF** (vista de 1 plano del bitmap del FG) con doble buffer propio;
- sub-campos de un DPF que comparten modulos;
- cualquier capa que reutilice el bitmap de otra sin duplicarlo.

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
  geometría de fetch anterior hasta que el pipeline se recarga.

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
- Técnica RoboCod (soft DPF): `docs/reference/amiga/techniques/robocod-layered-scroll.md`.
- Drivers gráficos por estrategia de composición: `GRAPHICS_DRIVERS.md`.
- Plan de adopción: `docs/guides/roadmap/REFACTOR_PLAYFIELD_SCROLL.md`.
