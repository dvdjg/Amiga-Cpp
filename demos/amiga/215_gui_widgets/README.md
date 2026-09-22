# Demo 215 — GUI del engine (`eng::ui`)

Muestra la librería GUI del engine en hardware: `UiPainter`/`UiTheme`, widgets
(`Panel`/`Label`/`Button`/`CheckBox`/`RadioButton`/`EditBox`), `layout` y una ventana con título,
dibujados sobre `scene.surface()` (un `field::Surface`) sin que la demo vea planos ni punteros.

## Estado: **NO VERIFICADA (borrador G8)**

La demo **compila y alcanza READY** (`detail=0x6e`, palabras de copperlist) sobre una escena EHB
320×256, pero el **renderizado de los widgets no está resuelto**:

- Se pintan los **paneles** (el `fill` del escritorio/ventana y su sombra), pero **no** el texto,
  las líneas del bisel ni la cara de los widgets.
- Aislado con sondas: con el árbol apagado, `Surface::fill_rect`/`draw_line`/`draw_text` y los
  colores de la paleta **sí** funcionan; al pintar el árbol, solo llega el primer `fill`.
- No es la recursión de `draw_tree` (el dibujo manual da lo mismo) ni el rasterizador
  (`install_raster` no cambia el resultado). Apunta a **redibujado de pantalla completa durante la
  display activa** o a un matiz del `Surface`/escena; requiere sesión de depuración dedicada.

Regla `DEMO_VISUAL_DEBUG.md` §6.2: **no se da por terminada** hasta pasar la visión (Ollama) con
la secuencia coherente. La demo queda como andamiaje del hito G8.

## Compilar / ejecutar

```bash
bash ./tools/build/build-demo.sh demos/amiga/215_gui_widgets --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/215_gui_widgets --sequence-frames 4
```

## Qué usa

- `eng/ui/*`: `theme`, `painter`, `widgets`, `layout`, `window`, `context` (G0–G6).
- Escena planar EHB 320×256 (`graphics::composition`), patrón de la demo 077.
