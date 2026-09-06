# XYLimited (corkscrew 8-way): algoritmo genérico y diseño del API

> Documento de diseño (2026-09-06). Dos cosas en uno: (1) la referencia canónica del
> ALGORITMO XYLimited de Steger expresado en términos de plataforma (independiente de Amiga),
> y (2) un análisis objetivo del diseño interno del API del engine tal como quedó al portarlo,
> con la propuesta de capas limpias. Se escribe porque el nombre "X-Limited" es equívoco y
> porque el algoritmo se quedó mezclado con conceptos de escena (HUD, fg/bg) que no le
> pertenecen.

## 0. Aclaración de nombres (por qué "XYLimited" y no "XLimited")

La taxonomía de Georg Steger (ScrollingTricks) distingue:

| Fuente original | Movimiento | Técnica |
|---|---|---|
| `Scroller_XLimited` | Solo X | bitmap + columna entrante plane-shifted, sin split |
| `Scroller_XYLimited` | 8 direcciones | corkscrew: X-Limited + anillo vertical con **split** |
| `Scroller_XUnlimited` / `XYUnlimited` | — | variantes sin la "banda de guarda" |

La demo 201 y el `scroll_y=true` de este engine implementan **Scroller_XYLimited** (el
corkscrew: anillo vertical `display_height = viewport + 2*bloque`, staging arriba, y el display
da la vuelta con un split del Copper). El nombre del código (`XlimitedScene`,
`XLimitedPlayfield`, `XlimitedDisplayComposer`) hereda la familia "xlimited", pero cuando se
activa `scroll_y` (8-way) el algoritmo correcto es **XYLimited**.

```
  XLimited (solo X)              XYLimited (8-way / corkscrew)
  ┌──────────────────┐           ┌──────────────────────────────┐
  │ columna entrante │           │ anillo vertical de display   │
  │ plane-shifted    │           │ + staging + split + saveword │
  │ sin split        │           └──────────────────────────────┘
  └──────────────────┘
```

La clase del engine es una FAMILIA (tiene `ScrollMode::EightWay`,
`HorizontalOnly`, `VerticalOnly`, `OneDirection`), así que "xlimited" como nombre de la familia
es defendible, pero el nombre de la variante 8-way correcta es XYLimited. Al documentar o
renombrar, usar "XYLimited (corkscrew)" cuando `scroll_y=true`.

## 1. El algoritmo en términos de plataforma (genérico)

Este es el núcleo que se debe poder portar a "cualquier plataforma con una superficie de
scroll": Mega Drive (planos), SNES (fondos con ventana), etc. Nada de lo siguiente depende de
BPLxPT o del Copper; son conceptos de un *ring buffer* con una ventana de lectura.

### 1.1 Vocabulario

- **Mundo / mapa**: rejilla de tiles (lógica, puede ser toroidal o acotada).
- **Cámara**: posición lógica `(mapposx, mapposy)` en píxeles de mundo. Es la que mueve el
  juego; el display es consecuencia.
- **Superficie de scroll**: buffer en anillo que la plataforma lee para mostrar una ventana.
- **Bucle del display (`display_height`)**: altura del anillo que la ventana recorre. Es
  `alto_visible + staging` (típicamente 2 bloques). **Es un invariante del algoritmo, no una
  decisión de la escena.**
- **Banda de staging**: las filas/columnas del anillo donde se pre-pinta lo que va a entrar,
  antes de que la ventana lo revele.
- **Sub-paso atómico**: mover la cámara 1 px = pintar la columna/fila entrante y LUEGO avanzar
  el puntero de lectura (*paint-then-advance*). Garantiza que nunca se ve un píxel sin pintar.
- **Costura / *saveword***: la tira entrante puede caer "a caballo" de un límite del layout
  (en Amiga, el interleave planelínea). Se guarda la word que el blit va a pisar y se restaura
  al invertir la dirección.

### 1.2 Invariantes (los tres errores que costaron encontrar)

1. **El anillo se dimensiona para el área VISIBLE TOTAL del scroll, no para lo que quede tras
   restar un panel.** El corkscrew dibuja la columna entrante con `mapy` hasta
   `1+pasos_en_un_bloque` (con bloque 16: hasta 17). Si `display_height` es menor que ese
   rango, `mapy` colisiona en el módulo del bucle y las filas de abajo aparecen arriba.
   **Regla**: `display_height = alto_visible_del_scroll + 2*bloque`, SIEMPRE, aunque un HUD o
   panel ocupe parte de la pantalla (el panel es una capa aparte, no reduce el anillo).
2. **La fila de staging se envuelve en el bucle del display, no en la altura física del
   buffer.** La altura física incluye las filas extra que usa el *walk* horizontal; envolver
   ahí dibuja la banda entrante en filas que la ventana sí muestra al caminar en X.
3. **El origen visible (0,0) debe poder ser `map[0][0]`.** El hardware esconde los primeros
   16 px (1 bloque) de guarda; si el contenido no se desplaza, la primera fila/columna nunca se
   ve y el wrap "parece" desfasado 16 px. Un *bias visible* (desplazar el contenido 1 celda en
   el anillo) lo corrige.

### 1.3 Movimientos (ScrollRight/Left/Down/Up)

Cada movimiento, en 1 px:

- Pinta la tira entrante en la banda de staging (posiblemente *plane-shifted* si el layout es
  interleaved).
- Si al cruzar un límite de bloque se completa una columna/fila, ajusta la fila/columna de
  *fillup* opuesta (las dos bandas de staging se mueven y dependen entre sí).
- Guarda/restaura la costura (saveword) solo al cambiar de dirección.

El coste de Blitter por frame es ∝ al salto (`N` px ⇒ `N` sub-pasos), no al tamaño de la
pantalla: por eso el scroll puede ser infinito con memoria constante.

### 1.4 El "split" es un concepto de composición

Cuando la ventana cruza el final del bucle del display, la plataforma debe volver a leer el
inicio del anillo a mitad de frame. En Amiga eso es un **split del Copper** (reiniciar los
punteros BPLxPT en una línea). En otra plataforma sería un mecanismo equivalente. El algoritmo
solo debe EXPONER "la ventana da la vuelta en la fila `split_line`"; quién lo materialice es la
capa de composición/hardware. Por eso el `PlayfieldHardwareView` ya lleva
`display_offset`/`split_line`/`split_active`: es la costura correcta.

## 2. Mapa a Amiga y a las capas del engine

| Concepto genérico | Amiga | Capa del engine |
|---|---|---|
| Superficie de scroll (anillo) | Bitmap interleaved en Chip RAM | `XLimitedPlayfield` (posee el bitmap) |
| Bucle del display | `display_height` líneas del anillo | `m_display_height` |
| Staging + *plane-shift* | blit de la tira en `x=x0+bitmap_width` | `ScrollEngine::scroll_right/left` + `draw_block_job` |
| Costura | guarda de 1 word (saveword) | `save_word`/`restore_saveword` (sink) |
| Scroll fino/coarse X | `BPLCON1` + `BPLxPT` (planeaddx) | `PlayfieldHardwareView` |
| Vuelta del anillo | split del Copper | `XlimitedDisplayComposer` (lee `split_line`) |
| Mapa + cámara | `mapposx/y`, `videoposx/y` | `ScrollState` (en `ScrollEngine`) |

Punto clave: el **algoritmo** (los 4 movimientos + cámara + fillup) vive en
`ScrollEngine`, TEMPLADO sobre un *sink* genérico (`ScrollConsts` NTTP + convención de métodos:
`tile_width`, `display_height`, `map_wrap`, `add_draw`, `save_word`…). Eso ya es, de facto, la
"algorítmica genérica": si se documenta bien el contrato del sink, el mismo `ScrollEngine`
sirve para otra plataforma cuyo "sink" exponga un anillo con staging. Lo que NO está limpio es
el resto (ver §3).

## 3. Anatomía del diseño actual y crítica objetiva

### 3.1 Lo que está bien (conservar)

- **`Playfield` base** con hooks de mapeo lógico→físico (`planeline_for`, `byte_for`,
  `mirror_planelines`, `supports_walk`) implementa las primitivas UNA vez. El scroll es "una
  especialización del playfield", como reza su cabecera. Correcto.
- **`CanvasPlayfield`**: lienzo plano sin tiles ni scroll. Es exactamente la pieza que debe
  servir de HUD. Correcto.
- **`ScrollEngine` genérico sobre sink** + `fast_div` NTTP: el algoritmo no conoce el layout
  concreto. Correcto (y verificado: 0 `__udivsi3` en el bucle de 1 px).
- **`PlayfieldHardwareView`**: contrato playfield↔compositor que ya incluye el split. Correcto.
- **Los compositores** (`XlimitedDisplayComposer`, `DpfDisplayComposer`) separan la emisión del
  Copper de la lógica.

### 3.2 Problemas (con evidencia de esta sesión)

1. **El HUD está DENTRO de la clase del scroll.** `XlimitedSceneConfig` lleva `hud_height`,
   `hud_planes`, `hud_palette` y `XlimitedScene` posee `m_hud` y la zona overlay del Copper.
   El HUD (o panel de juego) es una capa de UI, no parte del algoritmo. **Evidencia de que esto
   es un defecto real**: el bug de "tres filas de tiles arriba que deberían ir abajo" existía
   porque la escena RESTABA el HUD del alto del campo (`fc.viewport_h = main_h`) y eso encogía
   el anillo del corkscrew por debajo del mínimo que el algoritmo necesita. Si el HUD fuera una
   capa independiente y el anillo se dimensionara por las reglas del algoritmo (§1.2), ese bug
   no podría existir.
2. **`bg`/`fg` son roles de composición, no del algoritmo.** `XlimitedScene` expone `bg()` y
   `fg()`, configura dual, parallax, `fg_canvas`… Todo eso es composición de capas con
   prioridad de profundidad. Querer "XYLimited solo en el fondo y un primer plano estático", o
   "un HUD con su propio scroll XYLimited", hoy no es expresable: el algoritmo está atado a la
   escena de dos capas fijas.
3. **El algoritmo se mezcla con un "path" de validación.** `update_auto`, `effect`,
   `start_phase`, `phase_frames` (el ciclo de 8 direcciones) son andamiaje de test, no un
   servicio reutilizable. Debería vivir en el lado de la demo, no en la clase reutilizable.
4. **La clase del scroll hace de "god-object config".** `XlimitedSceneConfig` junta geometría +
   HUD + tilebank + dual + parallax + path + sprites + paleta. Difícil de leer, probar y
   reutilizar por partes.
5. **Nombres equívocos.** `XlimitedScene` para la variante 8-way debería ser `XYlimitedScene`
   (o "corkscrew"); y llamar "Scene" a lo que en realidad es "escena de scroll XYLimited
   concreta con HUD" oscurece que una Scene genérica debería componer N capas.

### 3.3 Causa raíz

Se tomó un algoritmo de scroll y se le añadieron (en la misma clase) los adornos de una demo de
muestra: HUD, dual playfield, path de validación. El resultado es una clase cómoda para la demo
201 pero acoplada: ni es un algoritmo puro ni una Scene genérica. El diseño correcto separa
tres conceptos que hoy están fusionados: **superficie con scroll** (playfield), **composición
de capas** (Scene) y **algoritmo de scroll** (estrategia aplicada a una superficie).

## 4. Propuesta de capas limpias

```
┌──────────────────────────────────────────────────────────────┐
│ Scene (composición) = lista de capas con rol/profundidad      │
│   capa A: fondo con scroll XYLimited   (algoritmo XYLimited)  │
│   capa B: primer plano estático        (CanvasPlayfield)      │
│   capa C: HUD con o sin scroll propio  (Canvas ó XYLimited)   │
│   paleta(s) + sprites                                        │
└──────────────────────────────────────────────────────────────┘
        │ cada capa produce una PlayfieldHardwareView
        ▼
 Compositor (plataforma): emite el copper/frameset con los
 splits de las capas que den la vuelta en su bucle
```

- **`Playfield`/superficie**: memoria + geometría + mapeo + primitivas (ya existe).
- **Algoritmo** (`CorkscrewScroll`/`XYLimitedScroll`, hoy `ScrollEngine`): aplica un
  movimiento a una superficie que exponga el "sink" del anillo. Debe PODER aplicarse a
  cualquier `Playfield` compatible (fondo, y también a un HUD si se quiere).
- **Scene**: contenedor genérico de N capas (cada una un `Playfield` con su algoritmo o sin
  él) + rol de profundidad + paletas. SIN `hud_*`, SIN `fg/bg` fijos, SIN path de validación.
- **Compositor**: lee las `hardware_view` de las capas y materializa el frame (splits, EHB,
  overlay…). Es la única capa que conoce BPL/Cu/DDF.

### 4.1 Renombrado sugerido (fase 1, barato)

- `XLimitedPlayfield` → `XYLimitedPlayfield` (o mantener como familia y documentar).
- `XlimitedScene` → `XYLimitedScene`, y luego a una `Scene` genérica de capas.
- Extraer el path de validación a la demo (`TourDriver` ya es la mitad del camino).

### 4.2 Refactor estructural (fase 2, solo con objetivo)

- Sacar `m_hud`/`hud_*` de `XlimitedScene` → el HUD es un `CanvasPlayfield` que la demo
  compone como capa.
- Reemplazar el par fijo `field[2]`/`bg()/fg()` por un array de capas con rol.
- Mantener `ScrollEngine` como está (es el algoritmo bueno) y documentar el contrato del sink
  en un lugar visible para poder portarlo.

## 5. Cómo quedaría la demo 201 con el diseño limpio (visión)

```cpp
XYLimitedScene scene;                 // o una Scene de capas
scene.add_layer(scroll_cfg);          // capa 0: mapa XYLimited (anillo correcto)
scene.add_layer(hud_cfg);             // capa 1: CanvasPlayfield (HUD)
// la demo (no la escena) conduce el tour:
TourDriver tour;                      // ya separado en main.cpp
```

El tamaño del anillo de la capa 0 lo decide el algoritmo (no "viewport − hud"), y el HUD es
una capa independiente: ninguno de los tres bugs de §1.2 vuelve a ocurrir por acoplamiento.

---

> Puntos abiertos para decidir en el roadmap: (a) si `XlimitedScene` se convierte en la `Scene`
> genérica o se crea una nueva y `XlimitedScene` queda como adaptador de demo; (b) dónde vive el
> "split" como concepto reutilizable (ya está en `PlayfieldHardwareView`); (c) si conviene una
> interfaz formal de "sink de scroll" (hoy es por convención) para que `ScrollEngine` sea
> portabilidad explícita.

### 5.1 Estado de implementación (2026-09-06)

Pasos ya aplicados y verificados (build 201+107; demo 201 correcta, 107 single OK):

- **Contrato del algoritmo explícito**: `concept ScrollSink` en `scroll_engine.hpp` +
  `static_assert` en `XLimitedPlayfield::begin`. El scroll es una función genérica de una
  superficie (anillo+staging), portable a otra plataforma.
- **Config de la escena separada por concepto**: `XlimitedSceneConfig` deja de ser un
  god-object plano. Ahora expresa `hud` (XlimitedOverlayConfig: overlay compuesto),
  `dpf` (XlimitedDualConfig: composición de capas/parallax) y `path` (XlimitedPathConfig:
  conductor de VALIDACIÓN del harness) como sub-configs con nombre, separadas del scroll
  (geometría+algoritmo, que queda plano).
- **Geometría corregida en la demo 107**: su `kMainDisplayH` (anillo NTTP) pasa a
  `viewport TOTAL + 2*tile`, alineado con la escena y la validación de `begin` (arregla el
  `0x10703`). Pendiente: la ruta DUAL de la 107 no alcanza READY en 40s en el emulador
  (init 3+3 pesado; la ruta single sí).

Trabajo futuro para una separación total: mover `update_auto`/`effect` fuera de
`XlimitedScene` a un driver de demo, y una `Scene` genérica que componga capas arbitrarias
(ver §4). Requiere re-autorar la demo 107 como consumidor de capas y validar su ruta dual.
