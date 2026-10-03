# 215 — capa de BOBs de la fachada: `Anim` ligada, orden por `z` y hojas heterogéneas

Tutorial de la **§5** de `ROADMAP_GAME_API`: el juego dibuja objetos con `screen().bobs(layer)`
sin ver `FramePlan`, `BobTarget`, `BlitJob` ni minterms.

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/os/215_app_bobs --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/os/215_app_bobs
```

## Qué demuestra

- **`scene::BobActor` con `Anim`**: cada actor lleva su `graphics::Anim` (vistas no propietarias);
  `BobLayer::tick()` la avanza y `emit` usa su frame, sin que el juego lleve el índice.
- **Orden por `z`**: los actores 0 y 1 arrancan solapados y se mueven juntos; el mayor `z` queda
  **delante** (se ve el rojo sobre el amarillo).
- **Hojas heterogéneas**: cada actor elige su hoja con `sheet_index` (`BobLayer::set_sheet(i, …)`);
  aquí dos hojas planares opacas (rojo con plano 0, amarillo con planos 0+1).
- **Fachada**: `screen().bobs(layer)` oculta el plan y el destino; el fondo se limpia con
  `fill_box` y `app.present()` ejecuta el plan de Blitter. Doble buffer (`display.buffers = 2`) sin
  tearing.

## Puntos clave

- La hoja es **planar opaca** (`BobDraw::Opaque`, sin máscara): `frame = [planos]` de `h` filas,
  cada fila `base+1` palabras (la última, guarda del barrel shifter). Ver `bob.hpp`.
- `READY` se marca a partir del frame 4 (como la 214): el primer render puede caer antes de que la
  captura vea un buffer publicado.
- HOST-354 cubre `BobActor`/`BobLayer` (defaults, `tick`, `sheet_index`, `emit`).

## Validación visual

Ollama describe las secuencias como cuadrados de colores moviéndose; inspección directa de un
frame: **4 objetos limpios**, con el rojo delante del amarillo (orden por `z`) y sin ghosting.
