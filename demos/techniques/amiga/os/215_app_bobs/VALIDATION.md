# 215 - app_bobs: informe de validación

Informe de validación de la demo (procedimiento F0–F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`). Pasada de perfilado y corrección 2026-10.

## Estado

**VERIFICADA (rendimiento)**. La imagen es correcta —4 actores (dos rojos, dos amarillos), el amarillo por delante del rojo cuando se solapan (`z`), sin restos ni ghosting— y la cadencia es de **49,92 fps / 1,002 campos/frame / 142.102 ciclos** en `A500_debug` (antes 24,94 fps / 2,005 campos). Deuda DT-030 cerrada para esta demo.

## Medidas (`A500_debug`, `tools/debug/measure-fps.mjs`, 20 s)

| Medida | Valor |
|---|---|
| fps emulado | 49,92 |
| campos/frame | 1,002 |
| ciclos/frame | 142.102 |

Perfil nativo (`tools/debug/winuae-profile.mjs 215_app_bobs A500_debug 4`):

| Métrica | Valor |
|---|---|
| DMA de Blitter (canal 5) | ~0,9k accesos/frame (antes 10,6k/31,0k) |
| DMA de bitplanes (canal 6) | 10.240 palabras/frame (2 planos) |
| CPU (canal 2) | ~29k accesos/frame |

## Corrección aplicada

1. **Limpieza acotada**: se elimina el `screen.fill_box` a pantalla completa por frame; el buffer se limpia entero **una vez** (nace sin limpiar) y cada frame se limpia solo la **huella previa** de cada actor con `clear_box` (una caja por actor, encolada en el plan). La demo pasa a un solo buffer (la huella previa es la única que hay que limpiar).
2. **Display de 2 planos**: la paleta usa índices 0..3, así que el modo pasa de 4 a 2 planos (mitad de fetch de bitplanes y de copper), sin cambiar lo que se ve.

El coste por frame baja de ~284k a ~142k ciclos: el frame vuelve a caber en un VBlank. La imagen no cambia (los actores siguen siendo opacos y se redibujan enteros cada frame; la limpieza de huella no deja restos).

## Alcance y límites

Cubierto: fps/ciclos, perfil nativo de 4 campos, imagen en secuencia sin artefactos (visión canónica, `src/VISION.md`), orden `z` en el solape. **SIN VERIFICAR**: release y máquinas distintas de A500.
