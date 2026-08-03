# Integración de efectos demoscene con el engine

Documento que describe cómo adaptar los efectos del repositorio **demoscene-repo** al engine de Cursor-Amiga-C, minimizando duplicación de código mediante una biblioteca de funciones reutilizables. Incluye: catálogo de técnicas, mapeo a APIs del engine, uso de `gui.c` como referencia, planes de UI más potente, y uso del overlay WinUAE para depuración.

Para el plan operativo completo de importacion por oleadas, ver [demoscene-repo-import-roadmap.md](demoscene-repo-import-roadmap.md).

---

## 1. Visión general del demoscene-repo

### 1.1 Estructura y estilo

- **Sistema de efectos** (`effect.h`): cada efecto define `Load`, `UnLoad`, `Init`, `Kill`, `Render`, `VBlank` (algunos NULL). El bucle principal llama `EffectLoad` → `EffectInit` → `EffectRun(Render cada frame)` → `EffectKill` → `EffectUnLoad`.
- **Librerías** (`lib/`): lib2d (matemáticas 2D, recorte Liang-Barsky/Sutherland-Hodgman), lib3d (objetos 3D, transformaciones, visibilidad, ordenación), libblit (BitmapCopy, BlitterFill, BlitterLine, etc.), libgfx (Copper, bitmaps, sprites, paletas), libgui (widgets), libp61/libpt/libahx (audio), libmisc (fx, sintab, sort).
- **Estilo de código**: C con tipos propios (`types.h`, `common.h`), fixed-point (`fx.h`, `normfx`, `SIN`, `COS`), macros (`EFFECT(...)`), funciones inline y asm en archivos separados (`.asm`). Uso intensivo de copper, blitter y VBL.
- **Diferencias con nuestro engine**: nuestro engine es más ligero, usa `engine_*` y `TakeSystem/FreeSystem`; el demoscene-repo tiene un sistema completo con NewBitmap, CopListT, GuiStateT, etc. La adaptación implica **traducir llamadas** a nuestras APIs o **añadir al engine** las funciones que falten para evitar reimplementar lógica en cada efecto.

### 1.2 Objetivo: biblioteca reutilizable

La idea es que el **engine** ofrezca una capa tan completa que los efectos se adapten sobre todo haciendo **llamadas a funciones del engine**, no reescribiendo lógica. Si un efecto usa `BlitterFillArea`, nuestro engine debe tener `engine_blit_fill` (o equivalente). Si usa `CopSetupBitplanes`, nuestro engine debe exponer `engine_copper_setup_bitplanes` o un patrón similar. El código específico del efecto (tablas, fórmulas, parámetros) se mantiene; la interacción con hardware va por el engine.

---

## 2. Catálogo de técnicas y mapeo al engine

Para cada técnica del catálogo demoscene (ver `demoscene-repo/docs/06-catalogo-efectos.md`), se indica qué APIs del engine (existentes o planificadas) permiten implementarla sin duplicar código.

| Técnica | Descripción | APIs engine necesarias | Estado |
|---------|-------------|------------------------|--------|
| **Technique lab (menú)** | Overlay BPL1MOD/BPL2MOD, frame counter; base para fichas en `doc/techniques/` | `engine_debug_*`, copper mínima, VBL `engine_frame_tick` | ✅ Actual (`app/effects/technique_lab/`) |
| **Scroll + BOBs** | Scroll de fondo, sprites/bobs animados | `engine_blit_clear`, `engine_blit_bob`, copper/BPLCON1 | ✅ Actual |
| **Plasma** | Tablas sin/cos, copper chunky 8×4, color por línea | `engine_copper_*`, paleta por línea, tablas fx | 🔲 Copper por línea |
| **Fire** | Buffer chunky, propagación, conversión planar | `engine_blit_*`, c2p (chunky→planar) | 🔲 c2p, blit area |
| **Circles** | Círculos por CPU o blitter | `engine_blit_circle` / `engine_draw_circle` | 🔲 |
| **Color cycling** | Rotación de paleta en VBL | `engine_palette_rotate`, VBL callback | 🔲 |
| **Wireframe 3D** | Líneas 3D→2D, back-face culling | lib3d-like: `engine_3d_transform`, `engine_3d_project`, `engine_draw_line` | 🔲 |
| **FlatShade 3D** | Polígonos rellenos, iluminación por cara | `engine_blit_fill_poly`, `engine_3d_*` | 🔲 3D blitter |
| **Floor** | Suelo con franjas, BPLCON1 por línea | Copper por línea, scroll horizontal | 🔲 |
| **Tilemaps** | Tiles 8×8, 16×16, scroll | `engine_tile_draw`, `engine_scroll_*` | 🔲 |
| **Text scroll** | Texto en bitmap, scroll vertical | `engine_blit_*`, fuente, scroll | 🔲 |
| **Transparency** | Mezcla de planos, minterms blitter | `engine_blit_copy_masked`, minterms | 🔲 |
| **Sprites** | Sprites hardware, prioridad | `engine_sprite_*`, copper sprites | 🔲 |
| **Audio** | P61, Protracker, AHX | `engine_audio_*` (wrapper) | 🔲 |
| **GUI (botones, labels)** | Widgets, eventos ratón | `engine_ui_*` (ver sección 3) | 🔲 |
| **Game of Life** | Blitter con minterms para vecinos | `engine_blit_*`, minterms | 🔲 |
| **Metaballs** | Isolíneas, blobs | CPU + `engine_blit_fill` | 🔲 |
| **Rotozoomer** | Rotación + zoom de bitmap | `engine_blit_*`, coordenadas transformadas | 🔲 |
| **Copper bars** | Barras de color por línea | Copper por línea | 🔲 |

**Leyenda**: ✅ disponible; 🔲 planificado o por implementar.

### 2.1 Funciones del engine a priorizar para reutilización

Para que la mayoría de efectos se adapten con pocos cambios (alineado con [techniques/README.md](techniques/README.md) y [amiga-chipset-matrix.md](amiga-chipset-matrix.md)):

1. **Copper**: `engine_copper_set_planes`, `engine_copper_set_color`, `engine_copper_wait`, `engine_copper_set_colors_per_line` (para plasma, floor, neons, copper chunky).
2. **Blitter**: `engine_blit_fill`, `engine_blit_copy_area`, `engine_blit_line`, `engine_blit_copy_masked` (minterms configurables).
3. **2D**: `engine_2d_transform`, `engine_2d_clip_line`, `engine_2d_clip_polygon` (o integrar lib2d).
4. **3D**: `engine_3d_transform`, `engine_3d_project`, `engine_3d_face_visibility`, `engine_3d_sort_faces` (o integrar lib3d).
5. **Paleta**: `engine_palette_set`, `engine_palette_rotate`, `engine_palette_load`.
6. **Sprites**: `engine_sprite_set`, `engine_sprite_pos`.
7. **Audio**: `engine_audio_init`, `engine_audio_play_module`, `engine_audio_vblank`.

Cada efecto del demoscene-repo se mapearía así: sus llamadas a libblit, libgfx, lib2d, lib3d, libgui se sustituyen por `engine_*` equivalentes; el resto (fórmulas, tablas, datos) permanece.

---

## 3. GUI y menú: gui.c como referencia, UI más potente

### 3.1 gui.c del demoscene-repo

El efecto `effects/gui/gui.c` usa **libgui** con:
- **Widgets**: GROUP, FRAME, LABEL, IMAGE, BUTTON, RADIOBT, TOGGLE.
- **Eventos**: `GuiHandleMouseEvent`, `PopEvent` (EV_MOUSE, EV_KEY, EV_GUI).
- **Definición declarativa**: `GUI_DEF`, `GUI_BUTTON`, `GUI_GROUP`, `WG_ITEM`, etc.
- **Render**: `GuiRedraw`, `GuiWidgetRedraw` sobre un `BitmapT *screen`.

Sirve de **referencia** para:
- Estructura de widgets (jerarquía, área, estado).
- Flujo evento → handler → redraw.
- Integración con ratón y teclado.

### 3.2 Infraestructura UI del engine (objetivo)

El engine debe ofrecer una **infraestructura más potente** para menús y UIs de juegos:

- **Layout flexible**: no solo posiciones fijas; soporte para listas, grids, centrado, padding.
- **Fuentes escalables**: render de texto en distintos tamaños (8×8, 16×16, proporcional si aplica).
- **Temas**: paleta de colores para activo/inactivo, marco, fondo (como `GuiColorT` pero extensible).
- **Navegación**: teclado (flechas, Enter, Escape) además de ratón; útil para juegos sin ratón.
- **Layers**: posibilidad de HUD encima del playfield sin interferir.
- **Callback de acción**: al pulsar un botón o seleccionar, callback con contexto (qué widget, qué opción).

El menú principal de la app de demos será el primer cliente de esta UI. Más adelante, menús de juego (pausa, opciones, game over) usarán la misma infraestructura.

### 3.3 Plan de implementación

1. **Fase corta**: menú mínimo con `engine_ui_draw_text`, `engine_ui_draw_rect`, detección de clic en área (engine_mouse_left + coordenadas). Sin widgets complejos.
2. **Fase media**: `engine_ui_button`, `engine_ui_list` (lista de opciones con scroll), temas básicos.
3. **Fase larga**: sistema de widgets inspirado en libgui pero integrado al engine, con layout, fuentes y navegación por teclado.

---

## 4. Overlay WinUAE para depuración

### 4.1 APIs disponibles

El engine expone (vía `engine.h` y `support/gcc8_c_support.h`):

- `engine_debug_overlay_clear()` — borra el overlay.
- `engine_debug_rect(left, top, right, bottom, color)` — rectángulo.
- `engine_debug_filled_rect(...)` — rectángulo relleno.
- `engine_debug_text(left, top, text, color)` — texto.

Coordenadas en espacio PAL (0,0)-(768,576) aproximadamente (doble resolución para lowres).

### 4.2 Uso durante el desarrollo

La IA **usará el overlay** para que el usuario vea información útil en WinUAE:

- **Frame counter**, FPS, tiempo de frame.
- **Estado del efecto**: nombre, fase (init/loop/kill), errores.
- **Valores de depuración**: variables clave (posición cámara, número de objetos, flags).
- **Marcadores visuales**: rectángulos alrededor de zonas de interés (sprites, área de scroll).
- **Mensajes de error**: si algo falla, texto en pantalla en lugar de solo KPrintF.

Ejemplo en un efecto:

```c
engine_debug_overlay_clear();
engine_debug_text(10, 10, "Plasma", 0x00ff00ff);
engine_debug_filled_rect(10, 30, 100, 50, 0x00880088);
/* Mostrar frame o valores útiles */
```

### 4.3 Convención

Cuando la IA implemente o adapte un efecto, **añadirá llamadas al overlay** con información relevante para depuración. El usuario verá en WinUAE esos datos sin tener que depurar paso a paso. En builds de release se puede desactivar (macro o runtime) para no afectar rendimiento.

---

## 5. Estrategia de adaptación de efectos

### 5.1 Orden recomendado

1. **Efectos que ya cubre el engine** (scroll, BOBs): migrar primero; validan la estructura app/ y el menú.
2. **Efectos con pequeñas extensiones** (color cycling, paleta por línea): añadir `engine_palette_*` y copper helpers.
3. **Efectos 2D** (plasma, circles, shapes): añadir `engine_copper_*`, `engine_blit_line`, `engine_blit_fill`, lib2d o equivalentes.
4. **Efectos 3D** (wireframe, flatshade): añadir lib3d o `engine_3d_*`.
5. **Efectos complejos** (fire, tilezoomer, uvmap): combinar varias capas del engine.

### 5.2 Patrón de adaptación

Para cada efecto del demoscene-repo:

1. Identificar qué librerías usa (libblit, libgfx, lib2d, lib3d, libgui).
2. Mapear cada llamada a `engine_*` (si existe) o marcar como “falta en engine”.
3. Extraer la lógica específica del efecto (tablas, fórmulas) a un archivo `effect.c` en `app/effects/<nombre>/`.
4. Sustituir acceso a `custom` y a librerías externas por `engine_*`.
5. Añadir llamadas al overlay con datos útiles para depuración.
6. Probar en emulador; verificar que el comportamiento es correcto.

### 5.3 Código que puede permanecer en el efecto

- Tablas precalculadas (sin/cos, plasma, fire).
- Fórmulas y parámetros (amplitud, fase, velocidad).
- Datos de mallas (.mtl, etc.) y assets.
- Lógica de estado específica del efecto (contadores, transiciones).

Todo lo que sea “cómo hablar con el hardware” debe ir al engine.

---

## 6. Referencias

- **demoscene-repo**: repositorio en el workspace (p. ej. `../Amiga-C++/demoscene-repo` según estructura)
- **Catálogo**: `demoscene-repo/docs/06-catalogo-efectos.md` (si el repo está en el workspace)
- **Lib2d**: `demoscene-repo/docs/03-libreria-2d.md`
- **Lib3d**: `demoscene-repo/docs/04-libreria-3d.md`
- **gui.c**: `demoscene-repo/effects/gui/gui.c`
- **effect.h**: `demoscene-repo/include/effect.h`
- **Overlay**: `engine_debug_*` en `engine.h`; implementación en `support/gcc8_c_support.c`
- **Roadmap**: `doc/engine-roadmap.md`
