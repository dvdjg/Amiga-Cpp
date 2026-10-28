# Validación 207 - capas vivas animadas

## Estado

**Funcional corregida y validada visualmente.** `Flowers`, `Water` y la fogata se dibujan en su columna de mundo (`wx`); el cooker fusiona automáticamente la fogata 2×2 en un sprite 32×32 y el fondo real de cada celda se restaura con `MapCellSaveCacheSet` (save-under del surface), sin usar el tile cocinado. **Rendimiento: 28,06 fps de media, mínimo 22,19**; la media supera el suelo de 25, el HUD se refresca cada 4 frames y la parada de agua alcanza 25,50; el mínimo de ventana y el 50 Hz siguen abiertos.

## Medidas

| Build | fps | ciclos/frame | campos/frame | Notas |
|---|---:|---:|---:|---|
| `A500_release` visita con fusión + HUD/4 | 28,06 | 252 792 | 1,782 | 8 ventanas, 22,19-40,66 fps |
| `A500_release` parada de agua congelada (`DEMO_TOUR_STOP=1`) + HUD/4 | 25,50 | 278 222 | 1,961 | 2 campos estables |
| `A500_release` parada de agua sin parche (ablación) | 49,90 | 142 150 | 1,00 | el coste está en el parche, no en el scroll/HUD |

La ventana es **variable por diseño**: el desplazamiento no parchea capas vivas y las paradas sí. La media no se usa como presupuesto. El avance circular sin `%` por grupo (`__umodsi3`), el job único por celda y el HUD cada 4 frames subieron la parada de 18,18 a 25,50 fps.

## Contrato

- `tools/tiled/validate-cook-plan.mjs` valida el plan `beginning-fields.live-flowers`: `baked_through=Ground`, `ok=true` y avisos por `Road`, `Shadows`, `Object Shadows` y `RockSlopes_Auto` cocidos por encima de `Flowers`.
- Las celdas animadas usan el mismo destino que el bloque base: `target_x = wx` (mundo sin envolver), `ring_y = (display_offset + sy) mod display_height`; `sx`/`sy` sólo deciden visibilidad. `rx` no es el contrato del parche.
- `MapCellSaveCacheSet<96, 8>`: arena pequeña de 96 × 192 B (18 KB) y arena grande de 8 × 1,5 KB (12 KB) para sprites fusionados; captura una vez (`CopyRect` de todos los planos en un job), restaura antes de repintar (`RestoreRect`), no recaptura mientras la celda sigue visible y libera la entrada al salir de la ventana.
- El cooker fusiona clusters rectangulares de celdas contiguas con la misma cadencia y gids de un solo uso; la fogata 2×2 se emite como un grupo de 32×32 (un save-under y un blit por segmento). Agua/flores conservan sus celdas y se reparten con el cursor round-robin. La regla es general y no nombra la fogata.
- `HOST-499` valida `TileLayerStack`; `HOST-500` la geometría del anillo con un job por segmento (todos los planos); `HOST-502` el ciclo de vida y la geometría de `MapCellSaveCache`.
- Assets: `out/assets/tiled/beginning-fields-live-flowers/selected/` (73 grupos, 224 frames, 224 celdas, 47,6 KB de sheet y 7,9 KB de máscaras); la fusión elimina 3 grupos y 3 celdas sin cambiar el banco.
- La paleta la genera `cook-map` a partir de `palette-source.png` (todas las capas y objetos, incluidos los animados) y la cuantiza a 32 bases EHB con `quantize-ehb.mjs`; no hay paleta fija en el repositorio. La fuente incluye agua, flores y fuego aunque no estén en el bake.

## Secuencia

`A500_release`, parada de la fogata con la cámara congelada, 8 frames cada 100 ms:

- `run-status`: estado READY (`detail=0x20700000`), sin fallo de `AnimationDraw`.
- La fogata cambia de silueta con el anillo de piedras y la hierba estables; las flores blancas/rojas y el agua cambian sus frames. El diff de una región estática es 0; los recortes 4× de fogata y flores no muestran restos, huecos ni bloques negros/rojos.
- La fogata aparece en su celda base (pantalla 144 en la primera parada), no desplazada por la cámara.
- La paleta generada conserva agua azul con patrón y rojos de flores; el cuantizador incluye las capas vivas en `palette-source.png`.

## Visión local (Ollama)

- La pasada canónica sigue siendo un falso negativo global sobre detalles de 16 px: describe la escena como estática. La respuesta cruda queda en `src/VISION.md`; no se usa para dar la animación por válida.
- La evidencia que prevalece es la **secuencia** y el frame-diff, no el análisis por-frame aislado (un frame suelto no permite comparar). En la secuencia posterior a la fusión, Ollama describe el cambio de llamas de la fogata 32×32 y no observa huecos, bloques negros, halos ni restos del frame anterior; `frame-diff` muestra cambios localizados.
- El agua tiene 52 grupos y 161 celdas vivas; en la parada de agua congelada se ven sus celdas y sus frames cambian. La parada usa la primera celda del cluster, no el centro del bounding box.

## Telemetría Tiled

- Build `-DENG_METRICS=1`: `node tools/debug/metrics-report.mjs 207_tiled_live_layers_animation A500_eng_metrics1_release` lee el bloque `g_eng_metrics`.
- Build `-DDEMO_BLIT_STATS=1`: el HUD añade `J`, `B0/B1/B2` y `S/F` (entradas y fallos del cache). `DEMO_TOUR_STOP=N` congela una parada para capturar su secuencia.
- El contador de diferidos sigue siendo la señal para ajustar el reparto; el tope de 24 jobs no parte la fogata porque su capa se procesa primero.

## Deuda

- Rendimiento: media 28,06 pero mínimo 22,19 y sin 50 Hz estables; la parada de agua ya supera 25 con el HUD cada 4 frames. Perfilar la ventana más lenta y valorar doble buffer o repartir la entrada de celdas nuevas.
- Confirmar el encaje de las flores en celdas tapadas por `Road/Shadows` si se amplía la ventana; el aviso del validador sigue abierto.
- Repetir la validación de flicker de alta frecuencia cuando el coste permita una secuencia frame-step sin cuantización.
- Mantener el tope de 24 jobs y el orden por capas si se añaden más objetos compuestos; validar que cada compuesto cabe entero en un pase.
