# Optimización de capas de tiles animadas

Catálogo de optimizaciones para capas vivas de tiles (parche de animación por Blitter, save-under y coberturas), con el coste/beneficio medido en la demo 207 (`Beginning Fields`, EHB, 320×208). El objetivo es decidir **con datos** qué merece la pena implementar y qué no, porque varias ideas complican el algoritmo y solo compensan si el retorno es claro.

Medidas de referencia: `A500_release`, `tools/debug/measure-fps.mjs`, commit `33882349` + árbol de trabajo de 2026-10-29.

## 1. Punto de partida

| Elemento | Antes | Después de las optimizaciones de este documento |
|---|---:|---:|
| Grupos de animación | 113 | 98 |
| Frames en el banco | 264 | 247 |
| Celdas vivas | 224 (62 flores + 161 agua + 1 fogata) | 217 (61 + 155 + 1) |
| Coberturas | 40 | 27 (solo objetos opacos) |
| `anim_sheet.bin` | 55.296 B | 52.032 B |
| `anim_masks.bin` | 9.216 B | 6.272 B |
| Visita | 44,72 fps | 45,51 fps |
| Fogata (`DEMO_TOUR_STOP=0`) | 29,74 fps | 34,72 fps |
| Agua (`DEMO_TOUR_STOP=1`) | 41,93 fps | 46,82 fps |
| Flores (`DEMO_TOUR_STOP=2`) | 43,65 fps | 46,79 fps |

## 2. Catálogo

| Optimización | Mecanismo | Beneficio | Coste/riesgo | Estado |
|---|---|---|---|---|
| **Supresión por visibilidad** | Si un objeto estático (sombras incluidas) tapa >75 % de los píxeles opacos de la celda, se cuece (primer frame) y no se emite celda viva ni cobertura | 7 celdas vivas y 13 coberturas menos; −3,3 KB sheet, −2,9 KB masks | Bajo: umbral `kMinVisibleOpaquePercent=25` y `bake_animated_first_frame` ya existe | **Implementado** |
| **Cobertura solo de objetos opacos** | Las sombras cuentan para cocer la celda pero no se reestampan; su sprite sólido producía una cuña oscura (196 px, luma 44) junto al tocón | Elimina la cuña sin perder la sombra horneada; 27 coberturas | Bajo: filtro por nombre de capa de sombra en la cobertura | **Implementado** |
| **Corte del bucle de candidatos** | Con el `FramePlan` lleno no cabe otro grupo (mínimo 1 job): se deja de visitar candidatos y se conservan `changed` para el siguiente frame | `patch_build` 116,7 → 91,9k; fogata 30,3 → 33,5 fps | Bajo: un `break` tras el tope | **Implementado** |
| **Save-under interleaved de un solo blit** | El buffer se guarda en `[fila][plano][palabra]`; `capture`/`restore` recorren `h×planos` filas con `bitplane_count=1` (un `BLTSIZE`) en vez de 6 blits con esperas | Agua/flores +6 %; fogata +1 fps; sin memoria extra | Medio: layout interno del cache y validación planar/interleaved (HOST-502) | **Implementado** |
| **Máscara compartida** | Si todos los frames de un grupo comparten máscara, el banco guarda una sola y `kTileAnimationFlagSharedMask` hace que el runtime no avance el stride | `anim_masks.bin` 9,2 → 6,3 KB; sin coste de CPU | Bajo: un bit y una rama en `draw_group` | **Implementado** |
| **Reutilización por secuencia** | Grupos con los mismos frames+máscara comparten `first_frame`/`mask_offset` (clave de layout por gid) | Ya estaba; 113 grupos → 81 secuencias únicas | Bajo | **Implementado (previo)** |
| **Dedup de frames entre secuencias** | Hash de frame empaquetado → offset compartido; requiere tabla de offsets por frame (el runtime asume `first_frame × stride`) | 64,5 → 43,0 KB de frames (−21,5 KB medidos) | Medio: cambia el layout del sheet y el contrato de `TileAnimationGroupView` | Pendiente (solo si la memoria aprieta) |
| **Opacidad por frame** | El cooker marca qué frames son 100 % opacos; el runtime usa `CopyRect` y omite `restore` (el frame cubre la celda) | En este arte los frames opacos ya son grupos opacos enteros; ganancia medida nula | Medio: campo `opaque_mask` y decisión por frame | **Implementado (sin ganancia aquí)** |
| **Clusters de restore** | Agrupar celdas visibles adyacentes en rectángulos pequeños (1×4, 2×2) para un solo `restore` y varios draws | No aplica: el agua no restaura y las flores no son adyacentes | Alto: identidad estable de clusters, captura/release por geometría, orden por capas | **Descartado (medido)** |
| **Cookie-cut con canal C** | `D = (A&B) | (C&~B)` con el fondo guardado en C: un solo job por celda en vez de `restore`+cookie-cut | ~50 % de jobs y de Blitter en celdas enmascaradas | Alto: `BlitJob`/encoder/estado del Blitter; validar contra el AHRM | Pendiente |
| **Cobertura como MapSprite** | Reestampar el objeto completo (1 sprite) en vez de un frame 16×16 por celda cubierta | Menos frames/máscaras si un objeto cubre varias celdas | Medio: reutiliza `MapSpriteCache` de 208, pero cambia el cooker | Pendiente |
| **Frames interleaved de un solo blit** | Guardar cada frame como pares `[imagen][máscara]` por fila-plano (`make_interleaved_masked_bob`): una celda 16×16×6 se pinta con **un `BLTSIZE`** en vez de 6 | Medido: mixto 43,48 fps (fogata) / 47,60 (visita) vs planar 44,09/47,60, con +32,8 KB; el completo (104 KB) no cabe en 512K | Alto: cooker y `draw_group`; opción `animations.interleaved` | **Implementado como opción (sin ganancia aquí)** |
| **Plantillas de job** | Precalcular los campos estáticos de cada `BlitJob` (strides, minterm, fuente) y solo parchear destino/fuente | Menos `patch_build` (~91,9k actual en la fogata) | Medio: memoria de plantillas y riesgo de divergencia | Pendiente |
| **`sort_by_state` en el parche** | Agrupar jobs por estado común del Blitter | Menos reprogramaciones | **No seguro**: reordena `restore`/draw de la misma celda | Descartado salvo agrupar por celda |
| **Barrido de `set_tick`** | Anillo de 256 buckets con el próximo cambio por grupo; en el camino por frame solo se visitan los grupos que vencen | Visita 46,42 → 46,70 fps (banco mixto); fogata fija 35,69 → 37,41 | Bajo/medio: estado extra y reconstrucción en saltos de reloj | **Implementado (anillo)** |
| **HUD `DiffText`** | Reescribir solo los campos cambiados del texto | ~10k ciclos/frame de media; pico de 52k | Bajo, pero el HUD ya se refresca poco | Pendiente (bajo retorno) |
| **LOD de animación** | Divisor de tick por capa / política `preserve50` | Prototipo demo: el agua se congela por encima de ~1,25 campos y se reanima con histéresis; fogata 37,41 → 44,09 fps | Medio: API por capa y decisión de arte; el deadline/prioridad de engine sigue pendiente | **Prototipo demo (agua)** |

## 3. Protocolo de coste/beneficio

Cada optimización se evalúa con **A/B de una sola variable** y se acepta o se descarta con un criterio escrito antes de medir:

1. **Configuraciones**: `A500_release` (visita), `DEMO_TOUR_STOP=0/1/2` (fogata/agua/flores) y una ablación por feature (`DEMO_NO_COVER`, `DEMO_NO_PATCH`, o el define que añada la feature).
2. **Métricas**: fps emulado, ciclos/frame, campos/frame, mínimo/máximo por ventana; si la build es `DEMO_PROF`, `patch_build`/`patch_exec`; jobs y celdas del bloque de telemetría; bytes de Chip de los assets.
3. **Criterio de aceptación**: mejora del **peor frame** (`M`) ≥10 % **o** reducción ≥2 KB de Chip **sin regresión visual** (secuencia + frame-diff + Ollama). Si no llega, se documenta y no se implementa.
4. **Registro**: la tabla de resultados se guarda en el `VALIDATION.md` de la demo con commit, config y build; no se repite la medida si no cambia el código/config.
5. **Visual**: ninguna optimización se acepta solo por fps; el gate es la secuencia y el diff de píxeles.

### Herramienta

`tools/debug/bench-demo.mjs` ejecuta `measure-fps` para una lista de configs ya compiladas (con su `runner.uae`) y escribe una tabla Markdown con el delta frente a la primera:

```bash
node tools/debug/bench-demo.mjs 207_tiled_live_layers_animation \
  --configs A500_demo_tour_stop2_release,A500_demo_tour_stop0_release \
  --duration-ms 12000 \
  --assets out/assets/tiled/beginning-fields-live-flowers/selected
```

No compila ni lanza el emulador: el A/B debe usar builds y configuraciones explícitas. Para una feature nueva, añade su `EXTRA_DEFINES` y su config al mismo comando.

## 4. Qué se ha descartado y por qué

- **Dedup de frames entre secuencias**: 21,5 KB medidos, pero exige cambiar el direccionamiento `first_frame × stride` por una tabla de offsets. No se implementa hasta que el perfil de memoria lo justifique; la memoria actual (sheet 50,8 KB + masks 6,1 KB) cabe en el pool de 104 KB de la demo.
- **`sort_by_state` en el parche**: los jobs `restore` y `draw` de la misma celda se solapan; reordenarlos rompe el resultado. Solo tendría sentido agrupando por celda y ordenando dentro de cada grupo, con retorno incierto.
- **Precomponer frames opacos de flores/agua** (42/95 KB): la opacidad por frame se implementó y en este arte no hay frames opacos dentro de grupos enmascarados; no hay nada que precomponer. El coste de memoria sigue siendo alto para el caso general.
- **Canal C**: es el mayor ahorro por celda, pero toca el encoder del Blitter y el contrato de `BlitJob`; se deja como línea de trabajo con medida previa.

## 5. Resultados de la campaña (207)

Medidas con `measure-fps.mjs` sobre `A500_release`, árbol de trabajo del 2026-10-29. La línea base previa a la campaña era visita 45,51 (mín. 39,22), fogata 34,72, agua 46,82, flores 46,79.

| Paso | Resultado | Veredicto |
|---|---|---|
| Anillo de `set_tick` | Visita 46,42 → 46,70; fogata con tope fijo 35,69 → 37,41 | **Aceptado** |
| Presupuesto por capas con histéresis | Fogata 37,41 → 44,09; visita 46,70 → 47,60; mínimo 35,7 → 44,0; agua 48,58 y flores 48,32 | **Aceptado (política de demo)** |
| Frames interleaved | Mixto 43,48 (fogata) / 47,60 (visita) vs planar 44,09/47,60, +32,8 KB; completo 104 KB no cabe en 512K | **Sin ganancia; opción apagada** |
| Opacidad por frame | Campo `opaque_mask` implementado; los frames opacos ya son grupos opacos | **Sin ganancia aquí** |
| Clusters de restore | El agua no restaura (silueta fija) y las flores no son adyacentes | **Descartado** |

Estado final: visita **47,60 fps** (149 037 ciclos, mín. 44,0), fogata **44,09** (160 900), agua **48,58** (146 009), flores **48,32** (146 819). El 50 Hz sigue abierto en la fogata y en la peor ventana de la visita.

## 6. Próximos pasos medibles

1. **Deadline/prioridad a nivel de engine**: convertir la política de demo (agua/histéresis) en `FrameDeadline` + clases del `FramePlan`; el prototipo y sus umbrales están en [`PRESUPUESTO_PRIORITARIO_FRAME.md`](PRESUPUESTO_PRIORITARIO_FRAME.md).
2. **Cookie-cut con canal C**: un solo job por celda enmascarada contra el fondo guardado; validar contra el AHRM y el encoder de `BlitJob`.
3. **Dedup de frames entre secuencias**: 21,5 KB medidos; solo si el perfil de memoria aprieta.
4. **HUD `DiffText`**: bajo retorno (~10k de media) pero barato; medir antes de tocar la API del HUD.
5. **Doble buffer**: reevaluar con el presupuesto por capas ya aplicado; hoy no compensa en 207.

