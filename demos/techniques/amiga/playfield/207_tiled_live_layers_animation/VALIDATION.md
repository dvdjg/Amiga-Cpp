# Validación 207 - capas vivas animadas

## Estado

**Funcional: validada con cámara fija.** `Flowers`, `Water` y la fogata están vivas y cambian en la secuencia. **Rendimiento: en el borde del suelo de 25 fps**, 24,25 fps; no se declara 50 fps.

## Medidas

| Build | fps | ciclos/frame | campos/frame | Notas |
|---|---:|---:|---:|---|
| `A500_release` live-flowers | 24,25 | 292 482 | 2,062 | 8 ventanas, 22,19-24,96 fps |
| `A500_release` live anterior | 22,71 | 312 315 | 2,20 | referencia de agua + fogata con flores cocidas |

La ventana es **inestable**; la media no se usa como presupuesto. El aumento de coste coincide con las tres capas vivas y la cámara fija que mantiene visibles las 227 celdas animadas.

## Contrato

- `tools/tiled/validate-cook-plan.mjs` valida el plan `beginning-fields.live-flowers`: `baked_through=Ground`, `ok=true` y avisos por `Road`, `Shadows`, `Object Shadows` y `RockSlopes_Auto` cocidos por encima de `Flowers`.
- `HOST-499` valida `TileLayerStack` (modos, `live_count`, `is_live`, prioridad).
- Assets: `out/assets/tiled/beginning-fields-live-flowers/selected/` (76 grupos, 248 frames, 227 celdas, 47,6 KB de sheet y 7,9 KB de máscaras).
- La paleta reproducible es `tools/tiled/fixtures/beginning-fields.live-flowers.palette.json`, derivada del palette EHB con un índice adicional de agua (`[135,199,223]` en el índice 21). Sin ese índice, los cuatro frames del agua colapsan al mismo color y el overlay parece estático aunque el reloj avance.

## Secuencia

`A500_release`, 12 frames cada 250 ms, cámara fija en `Y=300`:

- `run-status`: estado READY (`detail=0x20700000`), sin fallo de `AnimationDraw`.
- Frame-diff f0000→f0006: **976 px** en el recorte de flores, **976 px** en la fogata y **1.236 px** en el campo.
- Frame-diff f0000→f0009: **40 px** de agua (ondas pequeñas) y **140 px** de flores.
- f0000→f0011: **76 px** de agua y **140 px** de flores.
- Sin huecos negros, sin bloques ausentes y sin parpadeo de campos en la secuencia.

## Visión local (Ollama)

- Secuencia completa de 5-8 frames: detecta la fogata, pero **no** las flores ni el agua; es un falso negativo conocido en detalles de 16 px (el modelo también las declara estáticas en un recorte sin ellas).
- Recorte 3x de agua: confirma ondas/brillos que cambian y avanzan.
- Recorte 4x de flores: el modelo las describe como estáticas pese a que el diff objetivo mide 140-976 px cambiados y los frames muestran apertura/color distintos. Se conserva como evidencia el diff, no la descripción.

## Deuda

- Optimizar `patch_animations`/la composición de tres capas hasta el suelo de 25 fps; ahora el frame ocupa 2,062 campos.
- Confirmar el encaje de las flores en celdas tapadas por `Road/Shadows` si se amplía la ventana; el aviso del validador sigue abierto.
- Repetir la validación de flicker de alta frecuencia en 207 cuando el coste permita una secuencia frame-step sin cuantización.
