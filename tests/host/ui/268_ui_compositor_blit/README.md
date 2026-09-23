# HOST-268: compositor por `Surface::blit` (`present_blit`)

Test host de `eng/ui/compositor.hpp`: valida la ruta de composición **por `Surface::blit`**
(`Compositor::present_blit`), que copia cada backing con el `Rasterizer` (CPU o Blitter,
encolando `CopyRect` en el `FramePlan`) y compone el fondo por `fill_rect`.

## Qué comprueba

1. **Equivalencia**: `present_blit` produce **exactamente** el mismo resultado píxel a píxel que
   `present()` (por píxel) para ventanas alineadas a palabra (en host, el rasterizador es CPU →
   `copy_rect_cpu`).
2. **Vuelta a CPU**: una ventana **no alineada** (destino no copiable por el Blitter) cae al
   copiado por píxel de ese rect y el resultado sigue siendo correcto (ventana visible y
   escritorio/fondo intactos).

## Notas

- El Blitter exige **destino alineado a palabra** (`x % 16 == 0`, `w % 16 == 0`); `present_blit`
  detecta el caso y cae a CPU por rect, de modo que nunca corrompe.
- La ruta de hardware (`BlitterRaster::copy_rect` → `CopyRect` → `execute_frame_plan`) es la misma
  que usan otras demos; aquí se fija el **contrato de equivalencia** del compositor.

## Salida de referencia

```
OK: compositor por blit (equivalencia con el copiado por pixel) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/268_ui_compositor_blit
```
