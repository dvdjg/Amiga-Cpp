# Validación 207 - capas vivas animadas

## Estado

**Funcional corregida y validada visualmente.** `Flowers`, `Water` y la fogata se dibujan en su columna de mundo (`wx`), la fogata completa se actualiza en el mismo pase y el fondo real de cada celda se restaura con `MapCellSaveCache` (save-under del surface), sin usar el tile cocinado. **Rendimiento: 27,13 fps de media, mínimo 21,17**; la media supera el suelo de 25, pero el mínimo de ventana y el 50 Hz siguen abiertos.

## Medidas

| Build | fps | ciclos/frame | campos/frame | Notas |
|---|---:|---:|---:|---|
| `A500_release` visita | 27,13 | 261 468 | 1,84 | 8 ventanas, 21,17-36,05 fps |
| `A500_release` parada de agua congelada (`DEMO_TOUR_STOP=1`) | 24,71 | 287 046 | 2,02 | cuantiza a 2 campos |
| `A500_release` parada de agua sin parche (ablación) | 49,90 | 142 150 | 1,00 | el coste está en el parche, no en el scroll/HUD |

La ventana es **variable por diseño**: el desplazamiento no parchea capas vivas y las paradas sí. La media no se usa como presupuesto. El avance circular sin `%` por grupo (`__umodsi3`) y el job único por celda subieron la parada de 18,18 a 24,69 fps.

## Contrato

- `tools/tiled/validate-cook-plan.mjs` valida el plan `beginning-fields.live-flowers`: `baked_through=Ground`, `ok=true` y avisos por `Road`, `Shadows`, `Object Shadows` y `RockSlopes_Auto` cocidos por encima de `Flowers`.
- Las celdas animadas usan el mismo destino que el bloque base: `target_x = wx` (mundo sin envolver), `ring_y = (display_offset + sy) mod display_height`; `sx`/`sy` sólo deciden visibilidad. `rx` no es el contrato del parche.
- `MapCellSaveCache<96>`: arena única Chip de 96 × 192 B (18 KB); captura una vez (`CopyRect` de todos los planos en un job), restaura antes de repintar (`RestoreRect`), no recaptura mientras la celda sigue visible y libera la entrada al salir de la ventana.
- El parche recorre capas de primer plano a fondo: la fogata es un objeto 2×2 de Tiled cocinado como cuatro grupos de una celda de 16×16 (0, 3, 6, 9), no un sprite libre; caben juntos en el tope de 24 jobs. Agua/flores se reparten con el cursor round-robin.
- `HOST-499` valida `TileLayerStack`; `HOST-500` la geometría del anillo con un job por segmento (todos los planos); `HOST-496` el ciclo de vida y la geometría de `MapCellSaveCache`.
- Assets: `out/assets/tiled/beginning-fields-live-flowers/selected/` (76 grupos, 248 frames, 227 celdas, 47,6 KB de sheet y 7,9 KB de máscaras).
- La paleta la genera `cook-map` a partir de `palette-source.png` (todas las capas y objetos, incluidos los animados) y la cuantiza a 32 bases EHB con `quantize-ehb.mjs`; no hay paleta fija en el repositorio. La fuente incluye agua, flores y fuego aunque no estén en el bake.

## Secuencia

`A500_release`, parada de la fogata con la cámara congelada, 8 frames cada 100 ms:

- `run-status`: estado READY (`detail=0x20700000`), sin fallo de `AnimationDraw`.
- La fogata cambia de silueta con el anillo de piedras y la hierba estables; las flores blancas/rojas y el agua cambian sus frames. El diff de una región estática es 0; los recortes 4× de fogata y flores no muestran restos, huecos ni bloques negros/rojos.
- La fogata aparece en su celda base (pantalla 144 en la primera parada), no desplazada por la cámara.
- La paleta generada conserva agua azul con patrón y rojos de flores; el cuantizador incluye las capas vivas en `palette-source.png`.

## Visión local (Ollama)

- La pasada canónica detecta el desplazamiento de cámara, pero describe la escena como estática: falso negativo global sobre detalles de 16 px. La respuesta cruda queda en `src/VISION.md`.
- La evidencia que prevalece es el frame-diff de la secuencia y los recortes por entidad: fogata y flores cambian con la cámara quieta y el fondo estable. El modelo no sustituye esa comprobación.
- El agua tiene 52 grupos y 161 celdas vivas; en la parada de agua congelada se ven sus celdas y sus frames cambian. La parada usa la primera celda del cluster, no el centro del bounding box.

## Telemetría Tiled

- Build `-DENG_METRICS=1`: `node tools/debug/metrics-report.mjs 207_tiled_live_layers_animation A500_eng_metrics1_release` lee el bloque `g_eng_metrics`.
- Build `-DDEMO_BLIT_STATS=1`: el HUD añade `J`, `B0/B1/B2` y `S/F` (entradas y fallos del cache). `DEMO_TOUR_STOP=N` congela una parada para capturar su secuencia.
- El contador de diferidos sigue siendo la señal para ajustar el reparto; el tope de 24 jobs no parte la fogata porque su capa se procesa primero.

## Deuda

- Rendimiento: media 27,13 pero mínimo 21,17 y sin 50 Hz estables; perfilar la parada más lenta y valorar doble buffer o repartir la entrada de celdas nuevas.
- Confirmar el encaje de las flores en celdas tapadas por `Road/Shadows` si se amplía la ventana; el aviso del validador sigue abierto.
- Repetir la validación de flicker de alta frecuencia cuando el coste permita una secuencia frame-step sin cuantización.
- Mantener el tope de 24 jobs y el orden por capas si se añaden más objetos compuestos; validar que cada compuesto cabe entero en un pase.
