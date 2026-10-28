# Validación 207 - capas vivas animadas

## Estado

**Funcional corregida y validada visualmente.** `Flowers`, `Water` y la fogata se dibujan en su columna de mundo (`wx`); el cooker fusiona automáticamente la fogata 2×2 en un sprite 32×32 y el fondo real de cada celda se restaura con `MapCellSaveCacheSet` (save-under del surface), sin usar el tile cocinado. **Rendimiento: 37,95 fps de media, mínimo 30,51**; la media supera el suelo de 25, el HUD se refresca cada 4 frames y la parada de agua alcanza 35,88; el mínimo de ventana y el 50 Hz siguen abiertos.

## Medidas

| Build | fps | ciclos/frame | campos/frame | Notas |
|---|---:|---:|---:|---|
| `A500_release` visita con fusión + HUD/4 | 37,95 | 186 947 | 1,318 | 8 ventanas; mínimo 30,51 |
| `A500_release` parada de agua congelada (`DEMO_TOUR_STOP=1`) + HUD/4 | 35,88 | 197 690 | 1,393 | 2 campos estables |
| `A500_release` parada de flores (`DEMO_TOUR_STOP=2`) | 42,47 | 167 038 | 1,177 | solo flores; sin scroll |
| `A500_release` flores con `DEMO_DISABLE_LAYER=0` | 37,87 | 187 308 | 1,320 | sin el pase de flores |
| `A500_release` flores con `DEMO_DISABLE_LAYER=2` | 32,90 | 215 603 | 1,520 | el fuego está fuera de la ventana: no cambia |
| `A500_release` flores con `DEMO_NO_PATCH=1` | 49,97 | 141 962 | 1,001 | techo sin ningún parche |
| `A500_release` fogata (`DEMO_TOUR_STOP=0`) | 22,56 | 314 432 | 2,216 | peor caso |
| `A500_release` fogata con `DEMO_DISABLE_LAYER=1` | 37,44 | 189 469 | 1,335 | el agua es el coste dominante |
| `A500_release` fogata con `DEMO_DISABLE_LAYER=0` | 21,11 | 335 961 | 2,368 | las flores visibles cuestan ~4 fps |
| `A500_release` fogata con `DEMO_DISABLE_LAYER=2` | 17,45 | 406 619 | 2,866 | apagar el fuego solo ahorra ~0,2 fps |
| `A500_release` fogata con `DEMO_NO_PATCH=1` | 49,87 | 142 243 | 1,003 | techo sin parche |

La ventana es **variable por diseño**: el desplazamiento no parchea capas vivas y las paradas sí. La media no se usa como presupuesto. El avance circular sin `%` por grupo (`__umodsi3`), el job único por celda y el HUD cada 4 frames subieron la parada de 18,18 a 35,9 fps.

**Trabajo real, sin espera de VBlank (`W`) y peor frame (`M`).** La demo publica dos contadores independientes del tiempo de pared: `W` = ciclos consumidos entre el inicio de `update` y el final de `render`; `M` = peor `W` observado. El HUD los muestra como `W=... M=...`.

| Caso | fps | W (último) | M (peor) | P perfil |
|---|---:|---:|---:|---|
| Sin parche | 49,97 | 11.404 | 63.450 | `10/0/0/0/13` |
| Flores | 42,47 | 268.360 | 402.936 | `10/50/17/0/13` |
| Agua | 35,88 | — | — | `10/50/17/0/13` |
| Fogata | 22,56 | 101.980 | 678.942 | `12/93/38/0/13` |

Sin parche el **trabajo real** es ~11k ciclos y el peor frame ~63k; los 141.962 ciclos de la tabla son el **suelo de VBlank** (un campo PAL), no CPU. El Blitter está a 0 jobs. `W` y `M` son los indicadores correctos para saber cuánto margen queda antes de 50→25.

Con flores, el frame de parche puede llegar a **M=402.936 ciclos** (>2,8 campos): por eso el frame pierde VBlanks y la media cae a 42,47 aunque haya frames ligeros. El perfil `P=10/50/17/0/13` muestra que **50k son construcción del parche** y 17k Blitter; la composición ya se salta. El fuego está fuera de la ventana en esa parada y no aporta (M no cambia al apagarlo). En la fogata el peor frame es **678.942 ciclos** (>4,7 campos); ahí el agua visible domina y el fuego aporta solo ~0,2 fps porque es **un grupo fusionado** de 32×32. La deuda de 25 fps de versiones anteriores venía de un barrido más caro (224 celdas/73 grupos y dos lecturas); con la receta actual la media de flores es **42,47**, no 25, pero el **peor frame** sigue por encima de un campo.

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

- La visita mide **30,51 fps** en su ventana más lenta. La parada aislada de agua con el mismo build da **35,88 fps** (197.690 ciclos), así que el mínimo no está en esa parada.
- La parada de la fogata (`DEMO_TOUR_STOP=0`) es el peor caso medido: **22,56 fps**, 314.432 ciclos/frame, 2,216 campos. El perfil de secciones es `P=12/93/38/0/13` (scroll+execute / construcción del parche / execute / compose / HUD) y `N=17` jobs. El coste está en el pase de parche/Blitter de la escena completa, no en el HUD. Sin la fusión del objeto compuesto baja a **13,58 fps**: la fusión ayuda, pero no elimina el cuello.
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
