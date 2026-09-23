# Seguimiento 4 para grok — `fs_draw_edges` desfasa el contorno ~1-2 px (render roto)

**El render negro está resuelto**, pero la ruta asm **no produce la imagen correcta**. Resumen honesto con la evidencia.

## Lo arreglado (válido)

1. **`fs_update_face_visibility`**: la cámara (`cz`) se cargaba en `d4` y se reutilizaba como temporal en `v`/`mag2` → `pz` mal desde la 2ª cara. Ahora se recargan `cx/cy/cz` por cara.
2. **`.Lwait_blit` pisaba `d0`**: cargaba DMACONR en `d0`, que `fs_draw_edges` usa como BLTCON0 y escribe justo después del wait → se programaba DMACONR como con0 y **ningún blit de línea pintaba** (balón ausente, `nEdges=0`, sin crashear). Se salva/restaura `d0`.
3. **`BLTAPT` sin extender**: `move.l d3` dejaba la palabra alta de `derr` con basura. Se hace `ext.l d3` (acumulador de error de 32 bits), como `blitter_line_eor_draw`.
4. Añadidos `blt_signflag` cuando `derr<0` (como la ruta C++ del engine) y `DMACON` al inicio.

Tras esos fixes la ruta asm **llega a READY y `verify-116` da PASS** (~283k ciclos/frame ≈ 25 fps).

## El problema que queda: el contorno está desfasado

**Aislamiento**: con el `draw_edges` **C++** sobre la visibilidad/transform asm, el balón sale **perfecto**. Con el `fs_draw_edges` asm, sale roto ⇒ **el bug está en `fs_draw_edges`**.

**Medición del wireframe** (`-DFLATSHADE_SKIP_FILL=1`, ASM vs C++, **con el ángulo congelado** — `m_angle` fijo — porque si no cada captura cae en una fase distinta y la comparación no vale):

```
                   px de contorno   comunes
ASM                    2756           44
C++                    2796           44
```

Con el mismo ángulo, el contorno asm sale **fragmentado/truncado** (tramos cortos y punteados, el polígono no cierra) mientras el C++ dibuja las aristas completas. Eso apunta a que **el blit de línea se corta antes** (tamaño/`BLTSIZE` o el error inicial de `BLTAPT`/`BLTBMOD`/`BLTAMOD`), no a un desfase uniforme de 1-2 px.

**Nota**: añadí `blt_signflag` cuando `derr<0` copiándolo de `blitter_line`; pero **la ruta C++ que renderiza es `blitter_line_eor_prepare`, que NO lo pone** (ni el original). Lo he **quitado** para ser fiel a la referencia (el efecto sobre el desfase no se pudo aislar porque la primera comparación era entre ángulos distintos).

**Por qué rompe tanto**: el **area fill es XOR** (conmuta el relleno en cada píxel del contorno y lo **propaga por paridad de scanline**). Un desfase de 1-2 px cambia la paridad de una fila y la cascada entera se desmadra ⇒ bandas horizontales y triángulos sueltos. Por eso un desfase mínimo se convierte en una imagen catastrófica.

**Gate visual (Ollama, qwen3-vl)**: comparando la captura asm con la referencia C++ → veredicto **ROTO** (banda horizontal en la mitad inferior, contorno desalineado arriba/abajo, triángulos sueltos, sombreado inconsistente). Nota: **`verify-116` da PASS con la imagen rota** (cobertura y nº de tonos —incluso 15 tonos, más que los 7-8 correctos— no detectan un desfase de contorno).

## Lo que ya coincide (descartado)

Comparando `fs_draw_edges` con `AmigaBackend::blitter_line_eor_prepare`/`_draw` (la ruta C++ que sí renderiza):

- `bltcon0 = ror16(x0&15,4) | blt_line_eor` → el asm usa `ror.w #4` + `0x0b4a` (mismo `blt_line_eor`).
- `bltcon1` base = `blt_linemode|blt_onedot` + octante (`SUD`/`AUL`/`SUL`) → el asm usa `0x0013`/`0x0003` + `0x0004`/`0x0008`.
- `dmin<<1`, `derr = (dmin<<1)-dmax`, `bltamod = derr-dmax`, `bltbmod = dmin<<1`, `bltsize = (dmax<<6)+66`.
- `bltcpt = planes + (y0<<5) + ((x0>>3)&~1)`; `bltdpt = planes` (base); `bltapt = derr`.
- Registros comunes 1×/frame (afwm/alwm/adat/bdat/cmod/dmod) como `blitter_lines_eor_begin`.
- Escrituras de punteros de 32 bits (`write_custom_pointer`) → el asm también usa `move.l` (clobber idéntico de BLTBPT/BLTDPT).
- `VERTEX(i) = objdat + i + 6` y `EDGE(e) = objdat + e` → coinciden con `vertex3d`/`edge3d`.

## Preguntas

1. Con el desfase ~1-2 px y **sin faltar líneas**, ¿dónde mirarías primero: el **latch de `x0`** (`(x0>>3)&~1` + el `ror` del primer word), el **octante/signo** (`bltcon1`), o el **error inicial** (`BLTAPT=derr`)? ¿Cómo lo aislarías (p. ej. forzar un ángulo/vértices fijos y volcar los 8 registros por arista y compararlos con los del C++)?
2. El C++ calcula `row_offset = (y0<<5) + ((x0>>3)&~1)`; el asm hace `(y0<<5) + ((x0>>3)&~1)` con **shifts de 16 bits** (`lsl.w`/`lsr.w`). ¿Puede el uso de `.w` (en vez de `.l`) en algún paso introducir el desfase?
3. ¿Ves algún sitio donde el asm escriba los registros del Blitter en un orden que importe (p. ej. `BLTCPT` antes de `BLTAPT`, y ambos como `move.l` que pisan `BLTBPT`/`BLTDPT`)? El C++ usa otro orden.

**Repro**: `-DK_FLATSHADE_ASM=1` sobre `demos/amiga/116_flatshade_convex`; wireframe con `-DFLATSHADE_SKIP_FILL=1`. Referencia C++: `-DK_FLATSHADE_ASM=0`.
