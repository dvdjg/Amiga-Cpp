# Validación 207 - capas vivas animadas

## Estado

**Funcional corregida y validada visualmente.** `Flowers`, `Water` y la fogata se dibujan en su columna de mundo (`wx`); el cooker fusiona automáticamente la fogata 2×2 en un sprite 32×32 y el fondo real de cada celda se restaura con `MapCellSaveCacheSet` (save-under del surface), sin usar el tile cocinado. **Rendimiento: 37,90 fps de media, mínimo 30,51**; la media supera el suelo de 25, el HUD se refresca cada 4 frames y la parada de agua alcanza 35,86; el mínimo de ventana y el 50 Hz siguen abiertos.

## Medidas

| Build | fps | ciclos/frame | campos/frame | Notas |
|---|---:|---:|---:|---|
| `A500_release` visita con fusión + HUD/4 | 37,90 | 187 181 | 1,319 | 8 ventanas; mínimo 30,51 |
| `A500_release` parada de agua congelada (`DEMO_TOUR_STOP=1`) + HUD/4 | 35,86 | 197 809 | 1,394 | 2 campos estables |
| `A500_release` parada de flores (`DEMO_TOUR_STOP=2`) | 35,88 | 197 729 | 1,394 | solo flores; sin scroll |
| `A500_release` flores con `DEMO_DISABLE_LAYER=0` | 37,87 | 187 308 | 1,320 | sin el pase de flores |
| `A500_release` flores con `DEMO_DISABLE_LAYER=2` | 32,90 | 215 603 | 1,520 | el fuego está fuera de la ventana: no cambia |
| `A500_release` flores con `DEMO_NO_PATCH=1` | 49,97 | 141 962 | 1,001 | techo sin ningún parche |
| `A500_release` fogata (`DEMO_TOUR_STOP=0`) | 21,47 | 330 372 | 2,329 | peor caso |
| `A500_release` fogata con `DEMO_DISABLE_LAYER=1` | 37,44 | 189 469 | 1,335 | el agua es el coste dominante |
| `A500_release` fogata con `DEMO_DISABLE_LAYER=0` | 21,11 | 335 961 | 2,368 | las flores visibles cuestan ~4 fps |
| `A500_release` fogata con `DEMO_DISABLE_LAYER=2` | 17,45 | 406 619 | 2,866 | apagar el fuego solo ahorra ~0,2 fps |
| `A500_release` fogata con `DEMO_NO_PATCH=1` | 49,87 | 142 243 | 1,003 | techo sin parche |

La ventana es **variable por diseño**: el desplazamiento no parchea capas vivas y las paradas sí. La media no se usa como presupuesto. El avance circular sin `%` por grupo (`__umodsi3`), el job único por celda y el HUD cada 4 frames subieron la parada de 18,18 a 35,9 fps.

**Los 49,9 fps sin parche no son 142k de CPU.** El bucle de la demo espera VBlank: un campo PAL son ~141.984 ciclos, así que 141.962 es el **suelo de 50 Hz**, no trabajo. El perfil sin parche es `P=10/0/0/0/13`: unos **23k ciclos reales** (scroll/execute + HUD/4) y el resto es espera. El Blitter está a 0 jobs. Con el parche de flores el perfil es `P=10/50/17/0/13` (~90k): 50k de construcción y 17k de Blitter; la composición se salta cuando la cámara no cambia (0k), y aun así el frame supera un campo y cae a 35,9 fps. Si se quisiera bajar de ahí, el objetivo es el barrido de 50k, no el HUD.

**Cuentas de la parada de flores.** No hay scroll, pero el frame no es “solo flores”: el pase de parche recorre los 59 grupos y culla las celdas de agua/fuego fuera de la ventana; para las flores visibles construye `restore + capture + frame` (3 jobs por celda). El presupuesto de 24 jobs limita cuántas celdas entran por frame, pero no elimina el barrido. Medido: sin parche el frame cuesta **142.152 ciclos** (49,9 fps); con flores, **215.397** (32,9 fps). El parche añade **73.245 ciclos**, de los cuales ~51k son construcción y ~17k ejecución Blitter; el resto es el pase de métricas/plan. El fuego no explica nada en esta parada: está fuera de la ventana y apagarlo deja 215.603 ciclos (32,90 fps, diferencia <0,1%). El agua tampoco: 213.470 ciclos (33,23 fps). La deuda de 25 fps de versiones anteriores venía de ese barrido con 224 celdas/73 grupos y del pase de dos lecturas; con la receta actual son **32,9**, no 25. En la fogata, en cambio, el agua visible domina: 411.606 ciclos con todo frente a 189.469 sin agua (37,4 fps); el fuego solo pesa 5.187 ciclos (~0,2 fps) porque es **un grupo fusionado** de 32×32.

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

- La visita mide **30,51 fps** en su ventana más lenta. La parada aislada de agua con el mismo build da **35,86 fps** (197.809 ciclos), así que el mínimo no está en esa parada.
- La parada de la fogata (`DEMO_TOUR_STOP=0`) es el peor caso medido: **21,47 fps**, 330.372 ciclos/frame, 2,329 campos. El perfil de secciones es `P=12/93/38/0/13` (scroll+execute / construcción del parche / execute / compose / HUD) y `N=17` jobs. El coste está en el pase de parche/Blitter de la escena completa, no en el HUD. Sin la fusión del objeto compuesto baja a **13,58 fps**: la fusión ayuda, pero no elimina el cuello.
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

- Rendimiento: media 37,90 pero mínimo 30,51 y sin 50 Hz estables; la parada de agua ya supera 30 con el HUD cada 4 frames. La fogata sigue en 21,47 y el doble buffer medido no compensa en este frame. Perfilar el pase de parche y repartir la entrada de celdas nuevas.
- `build_tour`/las paradas siguen locales: solo 207 las consume. `ScrollTour` se extraerá cuando aparezca un segundo consumidor real, no antes.
- Confirmar el encaje de las flores en celdas tapadas por `Road/Shadows` si se amplía la ventana; el aviso del validador sigue abierto.
- Repetir la validación de flicker de alta frecuencia cuando el coste permita una secuencia frame-step sin cuantización.
- Mantener el tope de 24 jobs y el orden por capas si se añaden más objetos compuestos; validar que cada compuesto cabe entero en un pase.
