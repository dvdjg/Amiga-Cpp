# 214 - app_sprite: informe de validación

Informe de validación de la demo (procedimiento F0–F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`). Pasada de perfilado 2026-10.

## Estado

**ABIERTA (rendimiento)**. El render es correcto (fondo + sprite por `screen.sprite` + actor por `draw_world`, ambos visibles en la captura), pero el frame está muy por encima del presupuesto: **0,70 fps / 71,5 campos/frame / 10.150.143 ciclos**. Registrada como deuda en `ROADMAP_DEUDA_TECNICA.md` DT-030.

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

## Causa raíz (muestreo de PC en MCP)

Los PCs muestreados caen en `eng::materialize_bitmap_layers`
(`engine/include/eng/api/world_render.hpp`): cada frame vuelve a materializar la capa de bitmap
del `World` con `screen.fill(...)` **por tramo de fila**. El fondo de la 214 es un patrón de
damero/ruido (tramos de 1–2 px), así que el materializador hace del orden de **163.840
`fill` por frame** (~10 M ciclos); no hay Blitter implicado (canal 5 = 0).

No lo introducen ni los bounds del `FramePlan` ni la migración a `run_demo`: con la `main`
anterior y con los bounds desactivados la medida es la misma (A/B en DT-030).

## Plan de corrección (DT-030)

1. Materializar la fila indexada con **una operación por palabra** (expansión byte→planos) en
   vez de `fill` por tramo; o
2. pasar la capa por **C2P de Blitter** (el bitmap ya es byte por píxel) y publicarla en el plan;
3. mientras tanto, la demo puede evitarlo pre-renderizando el fondo en un asset planar del
   mismo modo que la pantalla y copiándolo con `Screen::bitmap` (Blitter), que es el camino
   que la 214 no usa.

## Alcance y límites

Cubierto: fps/ciclos y perfil nativo de 3 campos, PCs en `materialize_bitmap_layers`, imagen
correcta en captura. **SIN VERIFICAR**: la corrección del plan de arriba (propuesta, no
implementada) y el comportamiento en release.
