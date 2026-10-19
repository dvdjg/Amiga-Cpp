# Demo 087 — Triángulos sólidos por Blitter (pila de tests 3D, paso 1)

Primer eslabón de la **pila de tests** de la librería 3D acelerada por Blitter ([`ROADMAP_3D_BLITTER.md`](../../../../../docs/guides/roadmap/ROADMAP_3D_BLITTER.md), fase P2): siete triángulos sólidos se mueven por la pantalla, rotan en 2D, se solapan (painter por profundidad) y entran/salen por las cuatro esquinas. El pipeline es el del **paquete 1** de [`3D_GAME_ARCHITECTURE.md`](../../../../../docs/engine/architecture/3D_GAME_ARCHITECTURE.md): cámara → clip de cámara (near + laterales) → proyección acotada → culling por área → `Surface::fill_polygon` con `PolygonFillSink` (máscara 1 bpp + spans CPU + cookie-cut por plano en el Blitter).

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/087_blit3d_triangles --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/087_blit3d_triangles
# Filmina:
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/087_blit3d_triangles \
  --sequence-frames 6 --sequence-interval-ms 1000
```

## Técnica

- **Display**: 320×256×4 planos (paleta de 16), franja de depuración en las filas 232..255 del mismo bitmap (nombre, versión y contador de frames `F<num>` por palabras con `DiffText`); el área de juego se limpia cada frame con `blitter_clear` (4 blits).
- **Renderer** (`eng::graphics::render3d`): vértices en espacio de cámara (4.12) construidos desde coordenadas de pantalla a profundidad fija; clip de cámara **antes de dividir** (near + 4 laterales, con ajuste entero final) y proyección acotada al viewport; culling por área firmada del polígono proyectado; painter por profundidad entre triángulos (orden fijo por `z`).
- **Relleno**: `Surface::fill_polygon` (clip 2D HOST-479) delega en el `PolygonFillSink` del `CanvasPlayfield`: el backend limpia la máscara del bbox, la rellena con **spans CPU** (`convex_spans`, el mismo generador que la referencia) y **expande el color por cookie-cut** en 4 planos (Blitter). Es la ruta robusta: sin estado de paridad, no hay rayas en filas con un solo cruce.
- **Ablaciones**: `-DADV087_NO_CLEAR`, `-DADV087_NO_FACES`, `-DADV087_ONE_FACE`, `-DADV087_NO_FILL`, `-DADV087_NO_SINK`, `-DADV087_NULL_SINK` (perfilado por diferencia).

## Búsqueda previa

Patrón de bajo nivel ya abstraído y reutilizado (candidatos del engine verificados):

- **Render 3D**: `eng::graphics::render3d` (clip de cámara + proyección + culling + painter) sobre `math3d` (`mesh3d.hpp`) y `convex_spans` (`core/data/polygon.hpp`, HOST-479/HOST-481); no hay trigonometría ni proyección propias en la demo (`eng::retro::sin/cos` y `mul_wide`).
- **Relleno por Blitter**: `eng::amiga::PolygonFillService` + `Playfield::set_polygon_fill_sink` + `AmigaBackend::blitter_fill_polygon_strided` (no se programan registros del Blitter a mano).
- **Texto de HUD**: `eng::playfield::Surface::draw_text_words` y `DiffText` (`engine/include/eng/field/`), nunca píxel a píxel.
- **Escena/copper/clear**: `scene::compose` + `scene::display` + `blitter_clear` (patrón de la 203).

## Estado

Funcional y con **visión próxima a cerrar** (triángulos sólidos limpios, sin rayas ni halos; `src/VISION.md`), pero **ABIERTA por presupuesto**: 7,98 fps (6,27 campos/frame) con 7 triángulos de ~50 px. El perfil por secciones (`g_eng_prof`, arreglado el staging de `profile.mjs`) localiza el coste: ~110k del clear del área de juego y ~60k por cara en el **tiempo de Blitter de las 4 copias de plano** del relleno por cara (los spans CPU y el cálculo del renderer son baratos). La campaña de optimización (P5 del roadmap) debe reducir el trabajo de Blitter por cara (batch por plano para geometría disjunta, copias con menos sincronía o menor área); hasta entonces la demo queda como la referencia visual correcta de la pila. Medidas, alcance y ablaciones: `VALIDATION.md`.
