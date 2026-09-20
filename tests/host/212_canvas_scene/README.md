# HOST-212: `scene::compose` con `Surface` (interleaved y contiguo)

Test host del **contexto de dibujo `field::Surface`** sobre el modelo de escena
(`engine/include/eng/graphics/scene/compose.hpp`), en los dos layouts:

- **Interleaved**: `SceneResources.layout = Interleaved` enlaza un `field::CanvasPlayfield`.
- **Contiguo**: el layout por defecto enlaza un `field::ContiguousPlayfield` (planos uno tras
  otro, el de las escenas EHB/HAM).

En ambos, `Scene::surface()` devuelve un `Surface` con el mismo contrato
(`set_pixel`/`draw_line`/`fill_rect`/`fill_polygon`/`blit`), sin que la app vea planos,
punteros ni layouts.

## Qué comprueba

1. `compose` reserva bitmap + copperlist y la escena queda `ok()`.
2. `surface().fill_polygon` (interleaved y contiguo): interior con el color pedido, exterior
   vacío (lectura de color por plano).
3. `surface().draw_line` en contiguo cae en el plano correcto.
4. `surface().blit`/`blit_masked` en contiguo encolan **un `CopyRect`/`MaskedBobCookieCut`
   por plano** en el `FramePlan` (`blit_job_count() == planes`).
5. Doble buffer contiguo (`buffers = 2`): `commit()` avanza el buffer trasero.

## Salida de referencia

```
OK: scene::compose interleaved y contiguo (Surface + copperlist).
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/212_canvas_scene
```
