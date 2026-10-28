# Validación 207 - capas vivas animadas

## Estado

**Funcional corregida y validada visualmente.** `Flowers`, `Water` y la fogata se dibujan en su columna de mundo (`wx`); el cooker fusiona automáticamente la fogata 2×2 en un sprite 32×32 y el fondo real de cada celda se restaura con `MapCellSaveCacheSet` (save-under del surface), sin usar el tile cocinado. **Rendimiento: 41,63 fps de media, mínimo 36,16**; la media supera el suelo de 25, el HUD se refresca cada 4 frames y la parada de agua alcanza 41,93; el mínimo de ventana y el 50 Hz siguen abiertos.

## Medidas

| Build | fps | ciclos/frame | campos/frame | Notas |
|---|---:|---:|---:|---|
| `A500_release` visita con fusión + HUD/4 | 41,63 | 170 388 | 1,201 | 8 ventanas; mínimo 36,16 |
| `A500_release` parada de agua congelada (`DEMO_TOUR_STOP=1`) + HUD/4 | 41,93 | 169 185 | 1,192 | 2 campos estables |
| `A500_release` parada de flores (`DEMO_TOUR_STOP=2`) | 43,68 | 162 391 | 1,145 | solo flores; sin scroll |
| `A500_release` flores con `DEMO_NO_PATCH=1` | 49,97 | 141 962 | 1,001 | techo sin ningún parche |
| `A500_release` fogata (`DEMO_TOUR_STOP=0`) | 24,59 | 288 493 | 2,033 | peor caso |
| `A500_release` fogata con `DEMO_NO_PATCH=1` | 49,87 | 142 243 | 1,003 | techo sin parche |

La ventana es **variable por diseño**: el desplazamiento no parchea capas vivas y las paradas sí. La media no se usa como presupuesto. El avance circular sin `%` por grupo (`__umodsi3`), el job único por celda y el HUD cada 4 frames subieron la parada de 18,18 a 35,9 fps.

**Trabajo real, sin espera de VBlank (`W`) y peor frame (`M`).** La demo publica dos contadores independientes del tiempo de pared: `W` = ciclos consumidos entre el inicio de `update` y el final de `render`; `M` = peor `W` observado. El HUD los muestra como `W=... M=...`.

| Caso | fps | W (último) | M (peor) | P perfil |
|---|---:|---:|---:|---|
| Sin parche | 49,97 | 11.404 | 63.450 | `10/0/0/0/13` |
| Flores | 43,68 | 270.634 | 408.268 | `10/50/17/0/13` |
| Agua | 35,88 | — | — | `10/50/17/0/13` |
| Fogata | 24,59 | 17.650 | 202.562 | `12/93/38/0/13` |

Sin parche el **trabajo real** es ~11k ciclos y el peor frame ~63k; los 141.962 ciclos de la tabla son el **suelo de VBlank** (un campo PAL), no CPU. El Blitter está a 0 jobs. `W` y `M` son los indicadores correctos para saber cuánto margen queda antes de 50→25.

Con flores, el frame de parche puede llegar a **M=408.268 ciclos** (>2,8 campos): por eso pierde VBlanks y la media baja a 43,68 aunque haya frames ligeros. El perfil `P=10/50/17/0/13` muestra que **50k son construcción del parche** y 17k Blitter; la composición ya se salta. El fuego está fuera de la ventana en esa parada y no aporta. En la fogata el peor frame es **202.562 ciclos** con el tope de 12 jobs; ahí el agua visible domina y el fuego aporta solo ~0,2 fps porque es **un grupo fusionado** de 32×32.

**Presupuesto y evaluaciones de las propuestas.** El tope de parche bajó de 24 a **12 jobs**: con 9 un grupo de 4 celdas enmascaradas (12 jobs) nunca cabía y `pass_deferred` mantenía el redraw completo para siempre. Con 12, la fogata baja de M≈679k a **M≈203k**, pero flores sigue en **M≈408k**, por encima del objetivo de 141.984. El `M` ahora se resetea en el segundo frame de cada parada: el primero incluye la pasada de llegada, el resto mide régimen. Las **agrupaciones por bbox** no son viables en este mapa: los grupos de flores/agua están dispersos y sus bboxes miden hasta 448×560 px; no caben en la arena grande de 1,5 KB y capturarían zonas ajenas. La alternativa de **frames precompuestos opacos** costaría ~42 KB para flores, ~95 KB para agua y ~1,5 KB para el fuego; solo flores sería planteable con otro perfil de memoria, agua no. La optimización pendiente es agrupar **clusters de celdas visibles adyacentes** (no el grupo entero) y sacar el HUD de los frames de parche.

## Contrato

- `tools/tiled/validate-cook-plan.mjs` valida el plan `beginning-fields.live-flowers`: `baked_through=Ground`, `ok=true` y avisos por `Road`, `Shadows`, `Object Shadows` y `RockSlopes_Auto` cocidos por encima de `Flowers`.
- Las celdas animadas usan el mismo destino que el bloque base: `target_x = wx` (mundo sin envolver), `ring_y = (display_offset + sy) mod display_height`; `sx`/`sy` sólo deciden visibilidad. `rx` no es el contrato del parche.
- `MapCellSaveCacheSet<96, 8>`: arena pequeña de 96 × 192 B (18 KB) y arena grande de 8 × 1,5 KB (12 KB) para sprites fusionados; captura una vez (`CopyRect` de todos los planos en un job), restaura antes de repintar (`RestoreRect`), no recaptura mientras la celda sigue visible y libera la entrada al salir de la ventana.
- El cooker fusiona clusters rectangulares de celdas contiguas con la misma cadencia y gids de un solo uso; la fogata 2×2 se emite como un grupo de 32×32 (un save-under y un blit por segmento). Agua/flores conservan sus celdas y se reparten con el cursor round-robin. La regla es general y no nombra la fogata.
- `HOST-499` valida `TileLayerStack`; `HOST-500` la geometría del anillo con un job por segmento (todos los planos); `HOST-502` el ciclo de vida y la geometría de `MapCellSaveCache`.
- Assets: `out/assets/tiled/beginning-fields-live-flowers/selected/` (59 grupos, 220 frames, 156 celdas, 46,8 KB de sheet y 7,8 KB de máscaras); 34 celdas de agua bajo objetos estáticos quedan cocidas.
- La paleta la genera `cook-map` a partir de `palette-source.png` (todas las capas y objetos, incluidos los animados) y la cuantiza a 32 bases EHB con `quantize-ehb.mjs`; no hay paleta fija en el repositorio. La fuente incluye agua, flores y fuego aunque no estén en el bake.

## Secuencia

`A500_release`, parada de la fogata con la cámara congelada, 8 frames cada 100 ms:

- `run-status`: estado READY (`detail=0x20700000`), sin fallo de `AnimationDraw`.
- La fogata cambia de silueta con el anillo de piedras y la hierba estables; las flores blancas/rojas y el agua cambian sus frames. El diff de una región estática es 0; los recortes 4× de fogata y flores no muestran restos, huecos ni bloques negros/rojos.
- La fogata aparece en su celda base (pantalla 144 en la primera parada), no desplazada por la cámara.
- La paleta generada conserva agua azul con patrón y rojos de flores; el cuantizador incluye las capas vivas en `palette-source.png`.

## Análisis del mínimo

- La visita mide **36,16 fps** en su ventana más lenta. La parada aislada de agua con el mismo build da **41,93 fps** (169.185 ciclos), así que el mínimo no está en esa parada.
- La parada de la fogata (`DEMO_TOUR_STOP=0`) es el peor caso medido: **24,59 fps**, 288.493 ciclos/frame, 2,033 campos. El perfil de secciones es `P=12/93/38/0/13` (scroll+execute / construcción del parche / execute / compose / HUD) y `N=17` jobs. El coste está en el pase de parche/Blitter de la escena completa, no en el HUD. Sin la fusión del objeto compuesto baja a **13,58 fps**: la fusión ayuda, pero no elimina el cuello.
- **Doble buffer opcional**: el anillo actual es 352×288×6 planos = 76.032 B; dos buffers = 152.064 B. Esa reserva **sí cabe** en los 512 KB de un A500, pero no en el perfil de 128 KB de 207 y deja muy poco margen al sumar tilebank (~233 KB), sheet (~24,8 KB), save-under (~30 KB) y HUD. El motor ya tiene XlimitedSceneConfig::double_buffer, swap y réplica por bandas; el build DEMO_DOUBLE_BUFFER=1 usa 256 KB y se midió en A1200: **11,10 fps** frente a **15,99** del mismo build sin doble buffer. No se activa por defecto. A500 sigue sin poder reservar el segundo ring con el ejecutable cargado en Chip.

## Visión local (Ollama)

- La pasada canónica sigue siendo un falso negativo global sobre detalles de 16 px: describe la escena como estática. La respuesta cruda queda en `src/VISION.md`; no se usa para dar la animación por válida.
- La evidencia que prevalece es la **secuencia** y el frame-diff, no el análisis por-frame aislado (un frame suelto no permite comparar). En la secuencia posterior a la fusión, Ollama describe el cambio de llamas de la fogata 32×32 y no observa huecos, bloques negros, halos ni restos del frame anterior; `frame-diff` muestra cambios localizados.
- El agua tiene 44 grupos y 127 celdas vivas (34 celdas bajo objetos estáticos quedan cocidas); en la parada de agua congelada se ven sus celdas y sus frames cambian. La parada usa la primera celda del cluster, no el centro del bounding box.

## Telemetría Tiled

- Build `-DENG_METRICS=1`: `node tools/debug/metrics-report.mjs 207_tiled_live_layers_animation A500_eng_metrics1_release` lee el bloque `g_eng_metrics`.
- Build `-DDEMO_BLIT_STATS=1`: el HUD añade `J`, `B0/B1/B2` y `S/F` (entradas y fallos del cache). `DEMO_TOUR_STOP=N` congela una parada para capturar su secuencia.
- El contador de diferidos sigue siendo la señal para ajustar el reparto; el tope de 24 jobs no parte la fogata porque su capa se procesa primero.

## Deuda

- Rendimiento: media 37,90 pero mínimo 36,16 y sin 50 Hz estables; la parada de agua ya supera 30 con el HUD cada 4 frames. La fogata sigue en 21,47 y el doble buffer medido no compensa en este frame. Perfilar el pase de parche y repartir la entrada de celdas nuevas.
- `build_tour`/las paradas siguen locales: solo 207 las consume. `ScrollTour` se extraerá cuando aparezca un segundo consumidor real, no antes.
- Confirmar el encaje de las flores en celdas tapadas por `Road/Shadows` si se amplía la ventana; el aviso del validador sigue abierto.
- Repetir la validación de flicker de alta frecuencia cuando el coste permita una secuencia frame-step sin cuantización.
- Mantener el tope de 24 jobs y el orden por capas si se añaden más objetos compuestos; validar que cada compuesto cabe entero en un pase.
