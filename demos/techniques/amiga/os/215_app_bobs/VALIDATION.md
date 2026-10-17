# 215 - app_bobs: informe de validación

Informe de validación de la demo (procedimiento F0–F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`). Pasada de perfilado 2026-10.

## Estado

**ABIERTA (rendimiento)**. La imagen es correcta —secuencia de 6 frames con **4 objetos** (dos rojos, dos amarillos), el rojo por delante del amarillo (orden por `z`), sin ghosting— pero la cadencia es de **24,94 fps / 2,00 campos/frame / 284.489 ciclos**; el barrido de flota previo registraba 30,72 fps/1,63 campos. Deuda en DT-030.

## Medidas (`A500_debug`, `tools/debug/measure-fps.mjs`, 20 s)

| Medida | Valor |
|---|---|
| fps emulado | 24,94 |
| campos/frame | 2,005 |
| ciclos/frame | 284.489 |

Perfil nativo (`tools/debug/winuae-profile.mjs 215_app_bobs A500_debug 4`), alternando campos:

| Campo | CPU ocupada | DMA (canal 5 = Blitter) |
|---|---|---|
| par | 142.102 (0 % libre) | 10.586 accesos |
| impar | 142.102 (0 % libre) | 30.984 accesos |

## Interpretación

La demo limpia **la pantalla completa** con `screen.fill_box` (320×256×4) y luego emite los BOBs; el reparto de DMA (canal 5) refleja ese relleno + los objetos. El «CPU ocupada 100 %» incluye las **esperas activas** del Blitter (`wait_blitter`), por eso aparece saturado en ambos campos aunque el trabajo sea de Blitter. El A/B de la caída 30,72→24,94 descarta los límites del plan y la migración a `run_demo` (con la `main` anterior y sin bounds mide ~25,0; DT-030).

## Plan de corrección (DT-030)

1. No rellenar la pantalla completa por frame: limpiar solo la huella previa de los actores (o usar una capa `Fill` del `World`, que se materializa con relleno acotado).
2. Medir A/B con `screen.bobs`/streaming y confirmar 1 campo; registrar aquí la nueva medida.

## Alcance y límites

Cubierto: fps/ciclos, perfil nativo de 4 campos, imagen en secuencia (4 objetos, orden `z`). **SIN VERIFICAR**: la optimización propuesta y el modo release.
