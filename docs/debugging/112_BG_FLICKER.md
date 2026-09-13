# Demo 112 — Flicker de 1 px del fondo RoboCod (bitmap único): análisis y decisión

Bitácora de depuración del residuo de **1 px** que desplaza el plano de fondo (fondo FIJO) de la
demo `112_xlimited_robocod` sobre un **bitmap único** con Copper split. El objetivo es dejar por
escrito qué se verificó, qué se descartó y por qué se decide pasar a **doble buffer**.

## 1. Arquitectura relevante

- Amiga 500 OCS, PAL. Bitmap **único** interleaved de 5 planos (4 FG + 1 BG). Fila = 400 px =
  50 bytes; anillo `display_height = 288`; ventana visible `viewport_h = 208` (DIWSTRT en la
  línea 41). Blanking ≈ 105 líneas.
- Scroll XY-Limited con **Copper split** vertical: cuando `display_offset + 208 > 288`, el Copper
  espera en `split_line = 288 - display_offset` y recarga los `BPLxPT` al inicio del buffer. La
  ventana visible son dos trozos del bitmap.
- El plano 5 (BG) es una imagen 1 bit que debe verse **FIJA**: se reescribe el CONTENIDO cada
  frame con el Blitter (offset `src = -scroll_x`, sub-píxel con el barrel shifter; 2 rects para
  compensar el split).
- Ventana del blit: `bg_window_for(camx, period, fetch)` copia `[planeaddx-2, planeaddx+fetch)`;
  `fetch = viewport/8 + 2` (DDFSTRT=0x30 ya incluye la word extra del scroll fino → 22 words).
- Timing: las filas VISIBLES del fondo se escriben en el **blanking vertical** (espera a
  `kBlankStart = 41 + 208 = 249`), para no reescribir filas que el haz está mostrando.
- La copperlist es de doble buffer; el bitmap no.

## 2. Síntoma

- Con la cámara en movimiento, el fondo **parpadea 1 px** cada varios frames y sigue.
- Solo se percibe en **diagonal** cuando el scroll es abajo+derecha o arriba+izquierda (mismo
  signo en X e Y). En los otros dos diagonales no.
- Con la cámara parada no aparece.

## 3. Diagnósticos y resultados

| Diagnóstico | Qué comprueba | Resultado |
|---|---|---|
| `K_DIAG_FBCHECK` | Lee el plano 4 del **framebuffer** tras el blit y lo compara con el patrón fijo esperado | **`mism = 0`** en 14 frames (diagonal y solo-Y) |
| `K_DIAG_SKIP_SCROLL` + `K_INIT_CAMX` 16/17/32/80 | Compensación X con cámara congelada | Borde **idéntico** en los 4 casos |
| `K_DIAG_BG` | Ciclos del blit en el blanking | **25–31k ≈ 55–68 líneas** (de 105) → cabe |
| `K_DIAG_XONLY` / `K_DIAG_YONLY` | Aísla el eje del parpadeo | Desplazamiento **uniforme ±1 px** de TODAS las filas, según `d` |
| `K_EARLY_INSTALL` | Instala `COP1LC` en el blanking, antes del VBlank | El parpadeo **persiste** |
| Correlación con `dFrame` | ¿Depende del nº de VBlanks por frame? | No correlaciona |
| Lectura de `BPLCON1`/`BPL5PT` vía CPU | Registros reales del Copper | Devuelve **0** (lectura no fiable con el debugger; sin conclusión) |

## 4. Qué se descartó y por qué

- **Error de compensación (copia)**: el FBCHECK lee el bitmap y da 0 mismatches → el plano 4
  contiene exactamente el patrón fijo. Además, con cámara congelada el borde es idéntico para
  varios `camx`. No es un off-by-one de `src_x`/`d`.
- **Falta de ancho**: se corrigió a 22 words (el display fetcha 21 words desde `planeaddx`).
  La columna derecha ya se cubre.
- **Overrun del blit**: medido, cabe (55–68 < 105).
- **Fase copper↔bitmap por timing variable**: instalar `COP1LC` de forma anticipada
  (`K_EARLY_INSTALL`) no lo elimina.

## 5. Hipótesis viva

- El desplazamiento es **global** (uniforme en todas las filas) y depende de `d`, pero el bitmap
  es correcto y la copia es exacta. `camx` es constante en solo-Y, así que ni `planeaddx` ni
  `BPLCON1` cambian; el único valor que cambia es la fila de inicio del display (`planeaddy`).
- No se ha podido leer el registro real del Copper (la lectura por CPU devuelve 0 con el
  debugger), así que no se ha confirmado si el **`BPL5PT`/fila** que usa el display coincide con
  el esperado en los frames que hacen jitter.
- Alternativamente, la captura del canal lateral (WinUAE screendump) puede no estar sincronizada
  al haz y reflejar el bitmap a medio actualizar dentro del blanking. Como en solo-Y el contenido
  del bitmap cambia 1 px por frame, una captura pre/post-blit difiere exactamente 1 px → coherente
  con lo observado, pero no con que se vea "en vivo".

## 6. Por qué el soft-DPF de bitmap único es frágil

- El fondo FIJO obliga a **reescribir las filas visibles cada frame** (el contenido compensa la
  cámara). No hay un punto único y atómico de conmutación: el haz puede leer el bitmap a medio
  actualizar si cualquier sincronía falla (arranque del blit, IRQ, latencia del `COP1LC`).
- Aunque hoy el blit quepa en el blanking (~65 de 105 líneas), el margen se consume con la lógica
  del juego, y no hay **garantía** de atomicidad frame-a-frame.
- El parallax por puntero por plano (`BPL5PT`) no es una salida: `BPLCON1` (fino) es común a todos
  los planos en single playfield, así que el fondo tendría una sierra de hasta ~8–16 px.

## 7. Decisión

Pasar a **doble buffer**. Opciones, por coste:

1. **Doble buffer del plano de fondo** (recomendado si el problema es solo el BG): dos buffers del
   plano 4 con el MISMO stride interleaved (`display_height * planes * row` reservados por buffer,
   solo se usa el slot del plano 4), y el compositor alterna el puntero del plano 4
   (`BPL5PT`) entre ambos cada frame. El blit escribe el buffer trasero mientras el display lee el
   delantero; la conmutación ocurre en el VBlank. Coste ≈ 72 KB de Chip RAM (el stride interleaved
   obliga a reservar el hueco de los 5 planos aunque solo se use uno).
2. **Doble buffer del bitmap completo** (2 buffers de 5 planos): tear-free también para el FG,
   pero ~144 KB y hay que re-dibujar/copiar el anillo trasero.

El `K_DIAG_FBCHECK` y los modos `XONLY`/`YONLY`/`K_INIT_CAMX`/`K_DIAG_BG` se conservan como
diagnósticos opcionales en la demo para volver a medir tras el cambio a doble buffer.

## 7.1 Resolución (implementada)

Se implementó la opción 1:

- `XLimitedPlayfield` reserva **dos `gfx::Bitmap` de fondo** con el mismo layout interleaved
  (`m_bg_bitmap[2]`) y expone `bg_flip()` / `bg_double_buffered()`.
- `PlayfieldHardwareView` incorpora `bg_plane_base`; el compositor single lee el plano
  `parallax_plane` de ese buffer (y los demás de `real_base`), tanto en el bloque principal como
  en el reload del split.
- `make_bg_plane_copy_rect_job` escribe el buffer **trasero**; la demo llama `bg_flip()` tras el
  blit y **antes** de `compose()`, de modo que la copperlist del frame apunta al buffer recién
  escrito.
- **Resultado**: en solo-Y el borde del fondo pasa de oscilar 43/44 a ser **constante (43,38)** en
  los 14 frames → el flicker de 1 px **desaparece**. La causa confirmada era el tearing/fase de la
  reescritura sobre un único buffer.

## 7.2 Scroll independiente del fondo (soft DPF)

Con el fondo ya estable, se le da **cámara propia** (`m_bgscroll`, avanza 1 px/frame y rebota)
independiente del FG (±2 px/frame). El offset de contenido es
`src_x = m_bgscroll - camx (+dest*8)`, con la misma ventana (`bg_window_for`) y el mismo
barrel shifter; el doble buffer elimina el tearing. Esto es un **DPF soft con scroll independiente**
sobre un solo bitmap de 5 planos. Siguiente paso: sustituir la muestra del patrón por un
**tilemap XYLimited completo** (mapa + tileset + anillo/staging) para el plano de fondo,
reutilizando `ScrollEngine`.

## 8. Referencias

- Técnica y límites: `docs/reference/amiga/techniques/robocod-layered-scroll.md` §3.
- Invariante del anillo/altura: `demos/amiga/201_ehb_map/src/README.md` §7.
- Comportamiento del runner/emulador y capturas: `AGENTS.md`.
