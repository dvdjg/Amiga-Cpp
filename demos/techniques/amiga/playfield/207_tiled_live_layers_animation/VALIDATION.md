# Validación 207 - capas vivas animadas

## Estado

**Funcional: validada con visita de cámara.** `Flowers`, `Water` y la fogata están vivas; la cámara recorre el mapa con paradas de 3 s y las animaciones se actualizan en ellas. **Rendimiento: 40,53 fps de media**, mínimo 34,04 fps; no se declara 50 fps.

## Medidas

| Build | fps | ciclos/frame | campos/frame | Notas |
|---|---:|---:|---:|---|
| `A500_release` visita | 40,53 | 175 028 | 1,234 | 8 ventanas, 34,04-47,54 fps |
| `A500_release` live anterior | 22,71 | 312 315 | 2,20 | referencia de agua + fogata con flores cocidas |

La ventana es **variable por diseño**: el desplazamiento no parchea capas vivas y las paradas sí; la media no se usa como presupuesto y el mínimo medido queda por encima de 25 fps.

## Contrato

- `tools/tiled/validate-cook-plan.mjs` valida el plan `beginning-fields.live-flowers`: `baked_through=Ground`, `ok=true` y avisos por `Road`, `Shadows`, `Object Shadows` y `RockSlopes_Auto` cocidos por encima de `Flowers`.
- `HOST-499` valida `TileLayerStack` (modos, `live_count`, `is_live`, prioridad).
- Assets: `out/assets/tiled/beginning-fields-live-flowers/selected/` (76 grupos, 248 frames, 227 celdas, 47,6 KB de sheet y 7,9 KB de máscaras).
- La paleta la genera `cook-map` a partir de `palette-source.png` (todas las capas y objetos, incluidos los animados) y la cuantiza a 32 bases EHB con `quantize-ehb.mjs`; no hay paleta fija en el repositorio. La fuente incluye agua, flores y fuego aunque no estén en el bake.

## Secuencia

`A500_release`, 60 frames cada 250 ms, visita con paradas de 3 s:

- `run-status`: estado READY (`detail=0x20700000`), sin fallo de `AnimationDraw`.
- La secuencia muestra la visita completa y las paradas; el frame-diff incluye el scroll y las capas vivas. No hay pares idénticos ni huecos negros.
- La paleta generada conserva agua azul con patrón y rojos de flores; el cuantizador incluye las capas vivas en `palette-source.png`.
- Sin huecos negros, sin bloques ausentes y sin parpadeo de campos en la secuencia.

## Visión local (Ollama)

- La pasada canónica (8 frames de la secuencia de 90) detecta el **desplazamiento de cámara**, pero describe la escena como estática: falso negativo global sobre detalles de 16 px. La respuesta cruda queda en `src/VISION.md`.
- La evidencia que prevalece es el frame-diff de la secuencia y el recorte 4x de la fogata durante la primera parada: las llamas cambian de silueta entre frames consecutivos. El modelo no sustituye esa comprobación.
- Queda pendiente una pasada por entidad con recortes separados de `Flowers`, `Water` y fogata; no se declara cubierta por el modelo.

## Telemetría Tiled

- Build `-DENG_METRICS=1`: `node tools/debug/metrics-report.mjs 207_tiled_live_layers_animation A500_eng_metrics1_release` lee el bloque `g_eng_metrics`.
- Muestras reales: `tiled_groups_scanned` 18-47, `tiled_groups_patched` 1, `tiled_groups_deferred` 10-13, `tiled_cells_patched` 1, `tiled_cells_culled` 0-12. El contador de diferidos dispara un evento en el anillo cuando el plan no admite todos los grupos del turno.
- La lectura confirma que el coste no es solo “pintar”: una parte del barrido se descarta y otra queda pendiente. Es la señal que hay que usar antes de tocar el reparto de jobs.

## Deuda

- Optimizar si se quiere acercar a 50 fps; ahora el frame con visita ocupa 1,234 campos y el mínimo medido es 34,04 fps.
- Confirmar el encaje de las flores en celdas tapadas por `Road/Shadows` si se amplía la ventana; el aviso del validador sigue abierto.
- Repetir la validación de flicker de alta frecuencia en 207 cuando el coste permita una secuencia frame-step sin cuantización.
- Comparar el reparto de grupos con los contadores `tiled_groups_deferred` y `tiled_cells_culled` antes de cambiar el límite de 18 jobs o la paridad; vigilar que la cadencia de las tres capas siga siendo suficiente.
