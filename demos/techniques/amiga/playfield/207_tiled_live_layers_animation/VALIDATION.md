# Validación 207 - capas vivas animadas

## Estado

**Funcional y validada visualmente.** `Flowers`, `Water` y la fogata se dibujan en su columna de mundo (`wx`); el cooker fusiona la fogata 2×2 en un sprite 32×32 y el fondo real de cada celda con máscara variable se restaura con `MapCellSaveCacheSet`. **Ninguna celda animada visible queda congelada**: las 61 flores y las 155 celdas de agua se animan; las 27 que quedan parcialmente bajo un **objeto opaco** llevan una capa de cobertura que se reestampa después del parche, y las 7 que quedan casi totalmente tapadas (<25 % visibles) se cuecen. Las **sombras** cuentan para esa decisión de visibilidad pero no se reestampan (su sprite sólido producía una cuña negra). El agua, con máscara idéntica en todos sus frames, se repinta con un solo blit y sin save-under; el banco guarda una sola máscara por grupo; el save-under se copia con un solo blit por fila-plano. **Rendimiento: 47,60 fps de media en la visita, mínimo 44,0; fogata 44,09; agua 48,58; flores 48,32.** El presupuesto por capas congela el agua (ambiente) en los frames pesados y el anillo de `set_tick` solo visita los grupos cuyo cambio vence. El 50 Hz sigue abierto en la fogata y en la peor ventana de la visita.

## Medidas

Medidas con `tools/debug/measure-fps.mjs` sobre `A500_release`, build del árbol de trabajo sobre el commit `33882349` (2026-10-29).

| Build | fps | ciclos/frame | campos/frame | Notas |
|---|---:|---:|---:|---|
| `A500_release` visita | 47,60 | 149 037 | 1,050 | 8 ventanas; mínimo 44,0, máximo 49,9 |
| `A500_release` fogata (`DEMO_TOUR_STOP=0`) | 44,09 | 160 900 | 1,134 | con presupuesto por capas; sin él 37,41 |
| `A500_release` agua (`DEMO_TOUR_STOP=1`) | 48,58 | 146 009 | 1,029 | agua activa |
| `A500_release` flores (`DEMO_TOUR_STOP=2`) | 48,32 | 146 819 | 1,035 | agua activa |

La ventana es **variable por diseño**: el desplazamiento no parchea capas vivas y las paradas sí. La media no se usa como presupuesto.

**Trabajo real (`W`) y peor frame (`M`).** El HUD de la parada de la fogata lee `W=120 004` y `M=206 970`; ese `M` es la llegada con el redraw de todas las celdas visibles, no el régimen. Para que el HUD no se quede congelado cuando el parche encadena frames (la fogata), se fuerza un refresco cada 8 frames con parche pendiente. El parche sigue dominando; el presupuesto por capas recorta el trabajo decorativo (agua) cuando el frame se acerca al deadline de 1,25 campos.

**Qué cambió y por qué baja el coste:**

- **Caché de visibilidad por cámara**: `rebuild_anim_visible` calcula la tabla `grupo → celdas visibles (celda, target_x, ring_y)` al mover la cámara y el parche la reutiliza. Antes se recalculaban `wx/wy/ring_y` de todas las celdas de cada grupo cambiado en cada frame; con 57 grupos de agua diferidos era el coste dominante.
- **Candidatos por frame**: un solo barrido construye la lista de grupos cambiados (sin la capa de cobertura) y el orden por capas se aplica sobre esa lista. Se pasó de `capas × grupos` a `grupos + candidatos × capas`.
- **Contabilidad real de jobs**: una celda enmascarada de máscara variable encola `restore` + frame (2 jobs); la captura inicial sustituye al `restore`, no suma. Una celda opaca o de silueta fija encola 1; una cobertura suma 1 solo cuando su celda se repinta. El tope es 8 jobs/frame = el mayor grupo (4 celdas × 2); con 12 se midió peor (más pico por frame sin reducir el trabajo total).
- **Silueta fija**: el agua no usa save-under ni `restore`; el cookie-cut conserva el fondo. Es la optimización que no cuesta memoria (frente a precomponer frames opacos: 42 KB flores / 95 KB agua).
- **Supresión por visibilidad**: las 7 celdas con <25 % de su silueta opaca visible se cuecen en vez de mantenerlas vivas y con cobertura. Las sombras cuentan para esta decisión (se conserva su bake) pero no entran en la cobertura.
- **Máscara compartida**: los grupos de silueta fija guardan una sola máscara (`kTileAnimationFlagSharedMask`) en vez de una por frame. `anim_masks.bin` baja a 6,1 KB sin coste de CPU; el runtime solo consulta `mask_offset`.
- **Corte del bucle de candidatos**: cuando el `FramePlan` alcanza el tope de 8 jobs se deja de visitar candidatos (no cabe otro grupo, mínimo 1 job). Evita pagar el prólogo y el escaneo de decenas de grupos diferidos en cada frame; la fogata sube de 30,3 a 33,5 fps.
- **Save-under de un solo blit**: el buffer de save-under se guarda en el orden `[fila][plano][palabra]` del anillo; `capture`/`restore` recorren `h×planos` filas con `bitplane_count=1` y un solo `BLTSIZE` en vez de 6 blits con sus esperas. El destino planar conserva el camino multi-plano (HOST-502 cubre ambos). Sube agua/flores ~6 % y la fogata ~1 fps.
- **Cobertura de solo objetos opacos**: las sombras estáticas se quedan en el bake y no se reestampan; su sprite sólido producía una cuña de 196 px (luminancia 44) junto al tocón que la visión canónica no detectó. El pase de artefactos base-vs-vivo (`DEMO_NO_LIVE=1` + `dark-clusters.mjs`, §6.4.4 de `DEMO_VISUAL_DEBUG.md`) la caza objetivamente.
- **Anillo de vencimientos de `set_tick`**: el camino por frame solo recorre los grupos cuyo cambio vence en ese tick (256 buckets `m_head`/`m_link`, duraciones `u8`); antes se visitaban los 98 grupos cada frame. La visita subió de 46,42 a 46,70 fps (banco mixto) y la fogata sin presupuesto de 35,69 a 37,41. Un salto de reloj reconstruye el anillo.
- **Presupuesto por capas con histéresis**: con la ventana por encima de ~41 fps (1,25 campos) se congela el agua (capa ambiente) y se reanima tras 4 frames por encima de 45 fps; la fogata y las flores no se degradan. La fogata pasa de 37,41 (tope fijo de 8 jobs) a 44,09 fps; la visita de 46,70 a 47,60 y su mínimo de 35,7 a 44,0. El cursor round-robin ya existente reparte los grupos diferidos. `DEMO_NO_BUDGET=1` reproduce el tope fijo para el A/B.
- **Opacidad por frame**: `TileAnimationGroupView::opaque_mask` marca los frames sin transparencia; el parche los dibuja con copia y omite el restore porque cubren la celda. En este arte los frames opacos ya pertenecen a grupos opacos enteros, así que la mejora medida es nula; el campo queda listo para arte futuro.
- **Frames interleaved (opción)**: el banco mixto (agua planar + flores/fogata/coberturas interleaved) midió 43,48 fps en la fogata y 47,60 en la visita frente a 44,09/47,60 del planar, con 32,8 KB más de hoja; el banco completo (104 KB) no deja contiguos los 104 KB de pool en el A500 de 512K. El cooker lo emite solo con `animations.interleaved: true` en el plan; el defecto es planar.
- **Clusters de restore**: descartados. El agua es de silueta fija (no restaura) y las flores vivas no forman clusters adyacentes; agrupar restores no reduce jobs en este mapa. El save-under de un solo blit ya deja el restore en un `BLTSIZE`.

## Contrato

- `tools/tiled/validate-cook-plan.mjs` valida el plan `beginning-fields.live-flowers`: `baked_through=Ground`, `ok=true` y avisos por `Road`, `Shadows`, `Object Shadows` y `RockSlopes_Auto` cocidos por encima de `Flowers`.
- Las celdas animadas usan el mismo destino que el bloque base: `target_x = wx` (mundo sin envolver), `ring_y = (display_offset + sy) mod display_height`; `sx`/`sy` sólo deciden visibilidad.
- **Coberturas**: para cada celda viva, el cooker compara la cobertura real de los objetos estáticos de capas superiores (alfa > 0, con flips) con la unión de píxeles opacos de sus frames para decidir si la celda se cuece (sombras incluidas). La cobertura se emite solo con **objetos opacos** cuyo solape supera 2 px: un grupo de 1 frame 16×16 en la capa `Covers` (`TILED_ANIM_COVER_LAYER=3`). El runtime la dibuja justo después de la celda que tapa (`cover_layer` del campo), no como capa normal. El bbox del objeto ya no congela celdas; las sombras no se reestampan.
- `MapCellSaveCacheSet<96, 8>`: arena pequeña de 96 × 192 B (18 KB) y arena grande de 8 × 1,5 KB (12 KB) para sprites fusionados; captura una vez, restaura antes de repintar y libera al salir de la ventana. Las celdas de silueta fija no lo usan.
- El cooker fusiona clusters rectangulares de celdas contiguas con la misma cadencia y gids de un solo uso; la fogata 2×2 se emite como un grupo de 32×32.
- Assets: `out/assets/tiled/beginning-fields-live-flowers/selected/` (98 grupos, 247 frames, 244 celdas: 61 `Flowers`, 155 `Water`, 1 fogata y 27 coberturas de objetos opacos; `anim_sheet.bin` 50,8 KB y `anim_masks.bin` 6,1 KB). La máscara de silueta fija se emite una sola vez por grupo (`kTileAnimationFlagSharedMask`); la regla de visibilidad (sombras incluidas) cuece las 7 celdas con <25 % de silueta visible.
- `HOST-499` valida `TileLayerStack`; `HOST-500` la geometría del anillo con un job por segmento; `HOST-502` el ciclo de vida y la geometría de `MapCellSaveCache`. `tools/tiled/test-cook.mjs` cubre el cooker, incluida la fusión.

## Secuencia

Secuencias de `A500_release` con paradas congeladas, 8 frames cada 250 ms:

- **Flores junto a la banca de la fogata**: el recorte de la zona muestra que las flores blancas cambian de silueta en todos los frames. La banca es **bit-exacta** en el rectángulo exacto del sprite (0 píxeles distintos, 28×28 px, 8 frames), así que la cobertura la mantiene por encima del overlay. La consulta dirigida a Ollama responde: «Sí, las flores blancas cambian de forma/silueta entre capturas, y la banca de madera permanece idéntica en todas ellas».
- **Cuña oscura junto al tocón (defecto y arreglo)**: la cobertura reestampaba la sombra horneada como sprite sólido y dejaba una cuña de 196 px con luminancia 44 (34,51,34) junto a la flor y el tocón. El detector base-vs-vivo (`DEMO_NO_LIVE=1` + `dark-clusters.mjs`, región 440,120,220,160) reportaba `CLUSTER 196 px bbox 496,192..508,218` antes y `OK: sin clusters oscuros nuevos` después; la comparación visual muestra la sombra verde continua. Ollama confirmó la cuña antes del arreglo y después dio un falso positivo (el píxel más oscuro de la zona era marrón 85,51,34), por lo que la evidencia es el detector + el recorte. La causa y la regla quedan en §6.4.4 de `DEMO_VISUAL_DEBUG.md`.
- **Racimo de flores blancas superior**: la consulta dirigida a Ollama responde que ninguna flor blanca, incluidas las inferiores, permanece idéntica; el `frame-diff` marca cambios en las filas inferiores del racimo que antes quedaban congeladas.
- La fogata cambia de silueta con el anillo de piedras y la hierba estables; el agua y las flores cambian sus frames. El diff de una región estática es 0; los recortes 4× no muestran restos, huecos ni bloques negros/rojos.
- `run-status`: estado READY (`detail=0x20700000`), sin fallo de `AnimationDraw`.

## Visión local (Ollama)

- La pasada canónica (`src/VISION.md`, prompt sin pistas) describe la escena como estática: es un falso negativo global sobre detalles de 16 px y no se usa para dar la animación por válida.
- La evidencia que prevalece es la **secuencia**, el `frame-diff` y los recortes por entidad. Las preguntas dirigidas confirman la animación de las flores inferiores y que la banca de la cobertura no cambia ni recibe píxeles de flor. Los artefactos oscuros se cazan con el detector base-vs-vivo, no con una pregunta amplia (ver §6.4.4).
- Pasada final sobre 6 capturas de la visita: Ollama no ve huecos, píxeles negros ni bloques sólidos oscuros junto al tocón/banca, pero vuelve a describir la escena como estática (mismo falso negativo de detalle). El detector base-vs-vivo en la parada de la fogata (build final planar, presupuesto por capas) responde `OK: sin clusters oscuros nuevos`.

## Telemetría Tiled

- Build `-DENG_METRICS=1`: `node tools/debug/metrics-report.mjs 207_tiled_live_layers_animation A500_eng_metrics1_release` lee el bloque `g_eng_metrics`.
- Build `-DDEMO_BLIT_STATS=1`: el HUD añade `J`, `B0/B1/B2` y `S/F` (entradas y fallos del cache). `DEMO_TOUR_STOP=N` congela una parada; `DEMO_NO_COVER=1` desactiva las coberturas y `DEMO_NO_LIVE=1` deja solo la base cocida (referencia del pase de artefactos base-vs-vivo).
- `DEMO_DEBUG_ONE_CELL` mantiene la comprobación de un cookie-cut contra el valor esperado en `g_debug_anim_blit`.

## Deuda

- **50 Hz abierto**: la visita baja a **44,0** fps en su peor ventana y la fogata se queda en **44,1**; agua y flores están a ~48,5. El parche por celda y el Blitter de las ráfagas de agua/flores son el coste. El anillo de `set_tick` y el presupuesto por capas ya están aplicados; quedan el deadline/prioridad a nivel de engine (la política actual es de demo), el canal C del Blitter y el doble buffer opcional (256 KB, no compensa en este perfil). Los clusters de restore, la opacidad por frame y los frames interleaved se midieron y no pagan en este mapa (ver arriba).
- **Coberturas**: 27 celdas, solo objetos opacos, solape de borde tolerado ≤2 px; las sombras cuentan para cocer las celdas mayormente tapadas pero no se reestampan. Para objetos que cubren mucho más que un borde, la cobertura es estática (no un occluder dinámico); si un actor pasa por debajo, el pase `after_actors` de 208 es el camino, no esta capa.
- **HUD**: el refresco forzado cada 8 frames con parche pendiente evita que el contador se congele; el coste está incluido en las medidas.
- Repetir la validación de flicker de alta frecuencia cuando el coste permita una secuencia frame-step sin cuantización.
- Mantener el tope de 8 jobs y el orden por capas si se añaden más objetos compuestos; validar que cada compuesto cabe entero en un pase.
