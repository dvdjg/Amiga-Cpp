# 207 - Celdas animadas desplazadas y bitmaps incompletos

**Fecha:** 2026-10-27. **Estado:** abierto. Este documento recoge la opinión de Grok sobre el procedimiento, la corrige contra el código real y deja la primera prueba ejecutada.

## Opinión de Grok (resumen)

La propuesta de Grok mejora el método porque separa la cadena mundo → cámara → anillo → framebuffer, exige una celda aislada, un BlitJob y un checksum, y no acepta la descripción del modelo de visión como veredicto. La estructura de la escalera —host, bitmap offscreen, una celda, una capa, tres capas— es correcta.

Sin embargo, la fórmula propuesta no coincide con el contrato real del motor:

- `rx` no es `(wx - mapposx + display_offset) % 352`. `display_offset` es vertical. La referencia real es `rx = wx % bitmap_width`, en `engine/include/eng/field/xlimited_playfield.hpp:643`.
- `ry` no es `dmod2(wy - mapposy)`. La referencia real es `ry = dmod2(wy)`, en `xlimited_playfield.hpp:648`.
- La X de pantalla no es `rx - display_offset`. `display_offset` es la fila Y del anillo; la X visible la fija el compositor/hardware (`planeaddx`, `BPLCON1` y fine scroll).
- La dirección destino simplificada `ry * stride + (rx / 16) * 2` no cubre los 6 planos intercalados; hay que usar `BobTarget` (`row_bytes`, `plane_count`, `plane_step`) y el desplazamiento fino.
- La costura horizontal no es la vertical de `bob_draw_ring`: un tile con `rx + 16 > 352` cruza el final del anillo y necesita partirse o cullarse explícitamente.

**Corrección tras la prueba**: en este pipeline de tile animations, `add_draw` y `draw_group` usan la misma X que el bloque base, que es `sx` (pantalla), no `rx` (anillo de mundo). Cambiar el destino a `rx` produjo una **segunda fogata** en pantalla, con el fuego repetido en otra posición. `add_world_bitmap_masked` usa `rx`/`dmod2(wy)` porque es el contrato de BOBs de mundo; no es el oráculo del parche de tiles. El oráculo correcto para una celda animada es el **mismo camino que su bloque base**, comparando el rectángulo resultante. Culling en pantalla y destino del parche deben compartir `sx` y `ring_base + sy`, pero el destino no debe reinterpretarse como `rx`.

## Contrato real comprobado

Con una celda de mapa `(tx, ty)`:

```text
wx = tx * 16
wy = ty * 16
sx = (wx - mapposx) mod 640
sy = (wy - mapposy - 16) mod 640
target_x = sx
target_y = (display_offset + sy) mod display_height

# BOBs de mundo / add_world_bitmap_masked (NO el tile animator):
rx = wx % bitmap_width
ry = dmod2(wy)
```

El parche usa `target_x = sx` y `target_y = ring_base + sy`, el mismo camino que el bloque base. La prueba con `target_x = rx` generó una fogata duplicada; esa hipótesis queda refutada. `add_world_bitmap_masked` sigue siendo el oráculo para actores/BOBs de mundo, no para estas celdas.

## Prueba ejecutada: fase 0/1

Comando:

```bash
EXTRA_DEFINES="-DDEMO_BLIT_STATS=1" \
  bash tools/build/build-demo.sh demos/techniques/amiga/playfield/207_tiled_live_layers_animation --release
node dist/tools/run/run-demo.js demos/techniques/amiga/playfield/207_tiled_live_layers_animation \
  --config A500_demo_blit_stats1_release --settle-ms 1500
```

El HUD añade contadores por capa (`B0/B1/B2`). Resultado de la primera parada:

```text
F=43  X=112 Y=376  A=0 G=76 J=7 B0=1 B1=0 B2=0
```

- `B0=1`: se copió al menos una celda de flores, no todas estaban diferidas.
- `B1=0/B2=0`: en esa cámara no había celdas visibles de agua/fogata en el turno, o no cambiaron.
- Un grupo de 4 celdas necesita 24 jobs; el límite de 18 lo difería siempre. El límite se subió a 24.
- Medida release normal: ~30,9 fps de media con mínimo ~24,7; no alcanza el suelo de 25 de forma estable.
- Prueba de celda única: con `DEMO_DEBUG_ONE_CELL`, `TileAnimator.set_test_cell(5)` (Flowers, celda 5) y cámara congelada en `(544,552)`, la captura no muestra la flor y el HUD da `J=0 B0=0`. Reproduce el bitmap ausente en una escena mínima; aún hay que separar coordenada de la celda, culling y frame inicial. El filtro queda bajo macro de diagnóstico.

La prueba **no demuestra todavía** que todas las celdas estén completas: hay que aislar una celda de flores y comprobar el rectángulo de anillo con un checksum, como pide Grok.

## Recortes para Ollama

Se añadió `tools/analyze/ollama-crop.mjs`. Envía únicamente el recorte, sin HUD ni resto de escena:

```bash
node tools/analyze/ollama-crop.mjs captura.png \
  --crop 300,120,220,180 --scale 4 \
  --prompt "Describe únicamente este recorte de flores/fogata. No hables de otras zonas."
```

La prueba con un recorte de fogata/flores devolvió una descripción limitada a ese recorte. No se usa como veredicto: el veredicto debe ser checksum/diff de píxeles.

## Siguiente experimento

1. Aislar una celda de `Flowers` (grupo 2, `(4,1)` o una celda visible) con fondo sólido.
2. Calcular a mano `rx/ry` y leer por GDB el destino del BlitJob antes/después.
3. Repetir con cámara sin costura y con `rx` cerca de 352.
4. Solo entonces aplicar la corrección y probar una capa completa.

## Deuda

- El parche actual conserva `sx` en `add_draw/draw_group`; la corrección a `rx/ry` y la costura horizontal siguen pendientes.
- La caché de celdas que entran/salen está sin resolver; el límite de 24 evita el diferido sistemático de grupos de 4, pero no garantiza la entrada.
- La captura secuencial no es fiable para animación; usar pausa y lectura de memoria/checksum.
