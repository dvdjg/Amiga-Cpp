# Completitud y usabilidad de la GUI

Este documento fija qué debe considerarse una GUI completa y cómoda sobre `eng::ui`. La librería ya tiene un núcleo funcional: árbol intrusivo de widgets, foco, modales, dirty rects, layouts deterministas, `EditBox`, ventanas, backing stores, compositor y rutas CPU/Blitter. La siguiente etapa debe cerrar los contratos que permiten construir una aplicación sin callbacks ad hoc ni conocimiento del backend.

## Estado actual

```text
Entrada hardware / host
        │
        ▼
os::Msg / UiEvent                         ya existe, pero incompleto
        │
        ▼
UiContext: hit-test + foco + dispatch       ya existe
        │
        ├── Widget event handlers            básicos
        ├── layout + measure                 funcional, determinista
        └── dirty + compositor               funcional, con backing store
        │
        ▼
UiPainter → Surface → Rasterizer → FramePlan  integrado
        │
        ├── CPU: referencia y pequeños cambios
        ├── Blitter: fills, copies y texto por caché de glifos
        └── Copper: composición de display, paleta y publicación
```

La documentación de `GUI_LIBRARY.md` menciona `Text`, `MouseDrag` y `FocusLost`, pero
`engine/include/eng/ui/event.hpp` todavía no los define. `UiContext` tampoco procesa actualmente
`MouseMove`, `KeyUp` ni `JoyButton`. La API de eventos debe converger antes de ampliar el catálogo de
widgets.

## Abstracciones que faltan

### 1. Ciclo de vida de widget

`Widget` necesita un contrato explícito para estas fases:

```text
attach → measure → layout → update_state → paint → hit_test → detach
```

Debe distinguirse:

- `layout_dirty`: cambió geometría o métricas.
- `content_dirty`: cambió el contenido visual.
- `children_dirty`: cambió la composición de hijos.
- `interaction_dirty`: cambió foco, hover, pressed, selección o caret.

`mark_dirty()` no debería marcar siempre el mismo tipo de suciedad. El layout debe recalcularse solo
cuando cambia la geometría; un cambio de foco no debe rehacer medidas ni backings completos.

### 2. Coordenadas y transformaciones

El árbol necesita convertir de forma centralizada:

```text
pantalla → ventana → cliente → widget → contenido desplazado
```

El hit-test y el pintado deben usar la misma transformación. El scroll de una ventana, el borde, el
clip y el backing no pueden resolverse por separado en cada widget.

### 3. Propagación de eventos

El despacho debe ofrecer tres fases:

```text
capture: root → target
target:  widget receptor
bubble:  target → root
```

Cada handler debe poder devolver `Ignored`, `Handled` o `StopPropagation`. El modelo actual entrega
el evento directamente al widget seleccionado y no permite que un contenedor intercepte teclas,
gestos o comandos antes/después del hijo.

### 4. Captura y estados de interacción

Hay que separar:

- `focus`: receptor de teclado.
- `hover`: widget bajo el puntero.
- `pressed`: botón que inició el click.
- `pointer_capture`: widget que conserva el puntero durante un arrastre aunque el cursor salga de
  sus límites.
- `active_modal`: raíz que bloquea el resto del árbol.

La captura debe liberarse en `MouseUp`, cancelación, cierre de ventana o pérdida de foco global.
Esto es necesario para sliders, scrollbars, redimensionado y movimiento de ventanas.

### 5. Acciones semánticas

Los widgets no deberían depender de scancodes concretos. El input debe traducir a acciones:

```text
Accept, Cancel, NextFocus, PreviousFocus, MoveLeft, MoveRight,
MoveUp, MoveDown, PageUp, PageDown, Home, End, Delete, Copy,
Cut, Paste, SelectAll, Activate, ContextMenu
```

El scancode y el layout nacional son datos de la capa de entrada. La GUI consume `UiAction` y
`TextInput`, conservando `KeyDown`/`KeyUp` solo para controles que necesiten repetición o
combinaciones.

## Contrato de eventos recomendado

El evento debe separar posición, botón, tecla física, acción y texto. Un diseño mínimo es:

```cpp
enum class UiEventKind : u8 {
    PointerMove, PointerDown, PointerUp, PointerCancel,
    KeyDown, KeyUp, TextInput, Action, FocusIn, FocusOut, Tick
};

struct UiEvent {
    UiEventKind kind = UiEventKind::None;
    s16 x = 0;
    s16 y = 0;
    s16 dx = 0;
    s16 dy = 0;
    u8 pointer = 0;
    u8 buttons = 0;
    u16 key = 0;
    u32 codepoint = 0;
    UiAction action = UiAction::None;
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool amiga = false;
};
```

El buffer de eventos debe ser de capacidad fija, con política documentada cuando se llena. `TextInput`
no debe reconstruirse dentro de `EditBox` desde el rawkey: debe llegar como code point ya traducido.
La repetición de teclas debe ser responsabilidad del input service, no de cada widget.

## Texto y edición

`EditBox` ya soporta UTF-8 básico, caret, inserción, borrado y scroll horizontal. Para ser cómodo
necesita además:

- selección por rango (`anchor` + `caret`), incluyendo Shift+cursores;
- selección con ratón y doble click por palabra;
- navegación por palabra y por línea;
- `Home`/`End` de línea y `Ctrl+Home`/`Ctrl+End` de documento;
- `Delete` y `Backspace` sobre code points, no bytes;
- validación opcional por política: ASCII, Latin-1, UTF-8, numérico, ruta o longitud;
- undo/redo de capacidad fija;
- clipboard abstracto (`copy`, `cut`, `paste`) con backend Amiga/Workbench;
- placeholder, texto secreto, selección visible y estado de error;
- composición de teclas muertas y, si procede, entrada IME fuera del núcleo Amiga;
- cursor por hit-test de columna, no solo `x / 8`;
- desplazamiento horizontal por píxel o por columna según la fuente;
- notificaciones separadas para `text_changed`, `submit`, `cancel` y `selection_changed`.

El modelo recomendado mantiene un buffer externo o un `TextModel` de capacidad fija. `EditBox` debe
ser una vista/controlador del modelo y no mezclar almacenamiento, validación, render y callbacks.

## Widgets necesarios para una GUI de aplicación

El catálogo actual cubre controles básicos, slider, scrollbar y lista. Para completar la capa de
aplicación conviene añadir:

- `TextInput` reutilizable bajo `EditBox`.
- `ComboBox`/selector desplegable.
- `MenuBar`, `Menu`, `MenuItem` y atajos.
- `TabView`.
- `ProgressBar`.
- `Image`/`Icon` con asset y escala discreta.
- `TreeView` o lista jerárquica si el editor lo necesita.
- `Dialog` con resultado (`Accepted`, `Cancelled`) en vez de callback único.
- `Tooltip` y navegación de ayuda.
- `FocusScope` para formularios y diálogos.

No todos deben implementarse antes de usar la GUI. El mínimo cómodo para una aplicación es
`FocusScope` + `TextInput` + `Button` + `ListView` + `ScrollBar` + `Dialog` + `Menu`.

## Copper, Blitter y CPU

La GUI debe tratar los tres mecanismos como rutas de materialización, no como APIs que el widget
elige directamente.

### CPU

La CPU es la ruta por defecto para:

- texto corto o cambiante;
- caret, selección y pequeños glifos;
- marcos y cambios pequeños;
- regiones no alineadas;
- widgets que cambian una vez por interacción.

La CPU evita preparar máscaras y esperar al Blitter. También es la referencia de equivalencia para
los tests visuales.

### Blitter

El Blitter compensa cuando el trabajo es grande, alineado y repetitivo:

- limpiar fondos o paneles grandes;
- copiar backings a la pantalla;
- mover regiones rectangulares;
- rellenar áreas grandes;
- dibujar muchas líneas o glifos con caché de máscaras;
- componer ventanas con dirty rectangles alineados.

La escritura de caracteres **no debe acelerarse por Blitter indiscriminadamente**. El coste de
preparar máscara, scratch Chip, cookie-cut, cola y sincronización suele superar al CPU para una
etiqueta corta o un único caret. La ruta `glyph_cache` existente es útil para texto largo, repetido,
varias ventanas o fuentes reutilizadas, pero debe seleccionarse por umbral medido:

```text
texto corto/dinámico       → CPU
texto largo/repetido       → Blitter con GlyphCache
texto estático de ventana  → pintar una vez al backing; CPU o Blitter según tamaño
caret/selección pequeña    → CPU o dirty rect mínimo
```

No se debe lanzar un blit por carácter. La ruta Blitter debe agrupar glifos, reutilizar la máscara,
mantener scratch válido hasta ejecutar el `FramePlan` y evitar mezclar blits asíncronos con trazos
CPU que escriban la misma región sin una barrera.

### Copper

El Copper es adecuado para:

- publicar el doble buffer y punteros de display;
- cambios de paleta por banda o línea;
- cursor/overlay raster si la técnica lo requiere;
- splits de playfield y prioridad.

No es adecuado para pintar píxeles, glifos o fondos. La GUI debe emitir intenciones de paleta,
layout y publicación al plan Copper, que se materializa fuera de los widgets.

## Ciclo de frame recomendado

```text
input service
  → normaliza Msg a UiEvent/UiAction/TextInput
  → UiContext captura, enfoca y propaga
  → widgets actualizan modelo y dirty flags
  → layout solo si layout_dirty
  → paint solo backings/windows dirty
  → compositor genera CopyRect/fill en FramePlan
  → CopperPlan parchea paleta/punteros
  → ejecutar Blitter antes de publicar
  → VBlank: flip de display/Copper y Tick
```

El orden evita que la CPU escriba una región mientras el Blitter la está modificando. Las operaciones
asíncronas deben compartir un único `FramePlan` o una barrera explícita.

## Backlog de completitud

1. Alinear `UiEventKind` de código y documentación (`TextInput`, `MouseDrag`, `FocusLost`, cancelación).
2. Añadir captura de puntero, hover, movimiento y propagación capture/target/bubble.
3. Separar `UiAction` y `TextInput` de rawkeys, con repetición centralizada.
4. Introducir `layout_dirty`, `content_dirty`, `interaction_dirty` y transformaciones de coordenadas.
5. Completar `TextModel`/`EditBox`: selección, clipboard, undo/redo, validación y submit/cancel.
6. Añadir `FocusScope`, `Menu`, `ComboBox`, `TabView`, `ProgressBar` y resultado de diálogos.
7. Definir umbrales CPU/Blitter y probar texto corto, largo, estático y dinámico.
8. Integrar `CopperPlan` para paleta/publicación sin permitir Copper directo desde widgets.
9. Añadir pruebas host y una demo de formulario completa con teclado, ratón, modal, lista y edición.

## Criterios de completitud

- Una aplicación puede construir un formulario sin conocer `FramePlan`, `Surface`, registros ni
  backend.
- Todo input tiene una ruta única `Msg → UiEvent/UiAction/TextInput → UiContext`.
- El widget que inicia un arrastre conserva la captura hasta finalizarlo o cancelarlo.
- El foco, hover, selección y modal tienen estados observables y dirty rects mínimos.
- El texto se puede seleccionar, copiar, pegar, deshacer y validar con capacidad fija.
- La política CPU/Blitter se decide por el painter/rasterizer y por umbrales medidos.
- Copper solo materializa publicación, paleta y layout de display.
- Los tests comparan la imagen CPU de referencia con las rutas Blitter y compositor.
