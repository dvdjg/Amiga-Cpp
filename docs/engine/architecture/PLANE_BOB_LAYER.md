# PlaneBobLayer: BOBs de un plano

`eng::scene::PlaneBobLayer` es la capa de alto nivel para BOBs opacos de **un solo plano** sobre un framebuffer interleaved. Sustituye el patrón de la demo 210, que llamaba a `bob_draw_plane_opaque` desde el juego y no llevaba la huella previa.

## Contrato

- La hoja es un `eng::graphics::Bob` de **1 plano**, `BobDraw::Opaque`, con padding de color 0 alrededor del arte.
- Cada actor declara `x`, `y`, `frame`, `plane` y `visible`.
- `set_sheet(hoja, pad_x, pad_y)` fija la hoja y el padding por lado.
- `emit(plan, target)` dibuja los actores visibles. Si el movimiento de un actor cabe en el padding, basta la copia opaca; si no, la capa añade un `ClearRect` de su huella previa **en el mismo plano** antes del draw.
- `emit(plan, target, group_base)` marca el erase+draw de cada actor como grupo atómico (`group_base + i`) para el ejecutor beam-aware. El demo 210 no lo usa todavía porque el scheduler actual no cumple el presupuesto.
- La capa no usa heap, no conoce minterns ni módulos y no toca planos que no sean el del actor.

## Ejemplo

```cpp
eng::scene::PlaneBobLayer bobs;
bobs.set_sheet(sheet, 2, 2);
bobs.resize(6);
for (eng::u8 i = 0; i < 6; ++i) {
    bobs[i] = {x[i], y[i], frame[i], static_cast<eng::u8>(i % 5), true};
}
bobs.emit(plan, scene.bob_target());
```

El juego mueve los actores; la capa decide el borrado y la contabilidad de la huella. Para un salto mayor que el padding, el plan lleva dos jobs y el `FramePlan` los valida como cualquier otro blit.

## Límites medidos

- 6 BOBs con padding 2 y movimiento de 1 px: **49,9 fps**, secuencia de 30 frames limpia, un job por BOB.
- 8 BOBs: **42,5 fps**, fuera del presupuesto de 50; solo referencia.
- La agrupación beam-aware de un grupo por actor baja a 42,7 fps (6); no se activa por defecto.

## Tests

- `tests/host/graphics/505_plane_bob_layer`: movimiento dentro/fuera del padding, plano correcto, `BLTDMOD=196` y grupo atómico.
- `tests/amiga/l1_backend/007_bob_plane_base`: dos BOBs estáticos, canarios y `detail=31`.
- `tools/check/render-gates.sh`: HOST-504 + L1-007.

## Cuándo no usarla

- BOBs de varios planos o cookie-cut: usar `BobLayer`/`FastBobLayer`.
- Movimiento mayor que el padding en casi todos los frames: el clear extra duplica el coste; conviene doble buffer o rediseñar el movimiento.
- Beam-aware estricto a 50 fps: el backend agrupa y espera, pero el coste real de 13–14 líneas por BOB no cierra con 6 grupos; ver `docs/reference/amiga/techniques/beam-safe-bobs.md`.
