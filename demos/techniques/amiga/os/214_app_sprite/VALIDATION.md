# 214 - app_sprite: informe de validación

Informe de validación de la demo (procedimiento F0–F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`). Pasada de perfilado y ablaciones 2026-10.

## Estado

**ABIERTA (rendimiento)**. El render es correcto (fondo + sprite por `screen.sprite` + actor por `draw_world`, ambos visibles en la captura), pero el frame está muy por encima del presupuesto: **0,70 fps / 71,5 campos/frame / 10.150.143 ciclos**. Deuda en `ROADMAP_DEUDA_TECNICA.md` DT-030.

## Medidas (`A500_debug`, `tools/debug/measure-fps.mjs`, 20 s)

| Medida | Valor |
|---|---|
| fps emulado | 0,70 |
| campos/frame | 71,54 |
| ciclos/frame | 10.150.143 |

Perfil nativo (`tools/debug/winuae-profile.mjs 214_app_sprite A500_debug 3`):

| Métrica | Valor |
|---|---|
| CPU ocupada | **142.102 ciclos/campo, 0 % libre** (CPU saturada cada campo) |
| DMA por campo | cobre 1.248, CPU 23.578, bitplanes 20.480 (fetch nominal de 320×256×4) |
| DMA de Blitter | 0 accesos |

## Causa raíz (ablaciones 2026-10)

El coste está en la **materialización de la capa `Bitmap`** del `World` (`eng::materialize_bitmap_layers`, `engine/include/eng/api/world_render.hpp`), que se re-ejecuta cada frame. Ablaciones medidas:

| Variante | Resultado |
|---|---|
| `emit_row` sin `fill` (solo recorrer filas) | 49,92 fps / 1,00 campos |
| Solo la fila 0 (o solo la 255) | 49,87 fps / 1,00 campos |
| 2 filas | 46,46 fps / 1,08 campos |
| 1 run por fila (toda la fila, 256 runs) | 1,85 fps / 27,1 campos / 3,84 M |
| Materializador completo (≈555 runs) | 0,75 fps / 66,4 campos / 9,4 M |
| Igual en release | 1,89 fps (no es coste de debug) |

Conclusiones: el coste es **por fila materializada (~15 k ciclos/fila)**, no por run: una fila suelta es gratis y 256 filas cuestan ~3,8 M aunque cada una sea un único run de 320 px. `screen.fill_run` (tramo por CPU, sin el seam CPU/Blitter) y la regla «h == 1 → CPU» del `BlitterRaster` no cambian la medida: el coste vive **por debajo** (camino de escritura por palabra del playfield: hooks virtuales por palabra, RMW por plano y `mulu` por chunk). El patrón del fondo (damero de 1–2 px) multiplica el número de runs, pero incluso con runs largos la demo no cabe.

## Plan de corrección (DT-030)

1. **Escritor directo de fila indexada** en el playfield (punteros por plano calculados 1×/fila, stores sin RMW para los grupos completos, máscara solo en los bordes): objetivo ~600 ciclos/fila en vez de ~15 k. Es la opción «una operación por palabra» del plan original, pero a nivel de playfield (no de `Surface`).
2. Alternativa/además: **cache de materialización** (no re-materializar si ni la capa ni la cámara cambiaron) + scroll por hardware (BPLCON1/`Scene::set_fine_scroll`) sobre un bitmap planar pre-renderizado; es el camino de las capas de scroll del engine.
3. Medir A/B tras cada paso y actualizar esta tabla.

## Alcance y límites

Cubierto: fps/ciclos, perfil nativo de 3 campos, ablaciones por fila/run, imagen correcta en captura; el release se midió igual de lento (1,89 fps). **SIN VERIFICAR**: las correcciones propuestas (no implementadas).
