# 212_free_scroll_layer — **NO VERIFICADA (ROTA)**

> **Estado: NO VERIFICADA (roto).** El scroll del *Free Form Sprite Layer* está implementado a
> medias y **no cumple los fps**. No usar como referencia validada.

**Qué es (objetivo):** un **fondo de sprites HW a pantalla completa** (mundo 640 px = 40 columnas de
16 px × 256 líneas, 3 colores) con **scroll lateral 1 px/frame** sobre una **ventana** de 20
columnas, **no repetitivo**. Técnica *Free Form Sprite Layer* (Jeroen Knoester): 8 canales por DMA
(8 columnas) + el **Copper** reutilizando los canales para el resto (`POS`+`DATB`+`DATA`, sin
`SPRxCTL`; reposición de fin de línea).

## Por qué está roto

| Parte | Estado |
|---|---|
| **Emit-once** de la copperlist | OK (ya no se re-emite) |
| **`SPRxPOS`** con Blitter (`blitter_fill_words_strided`, fill strided, valor constante por columna; `VSTART` fijo) | **OK: 38.9 fps** (CPU libre) |
| **DATA (columna nueva)** con Blitter (`blitter_copy_words_strided`, copia A→D minterm `$F0`; mundo en Chip) | **ROTO: ~40M ciclos por cambio de columna → 1.3 fps** |

Diagnóstico: el blit de **FILL** (POS) es rápido, pero el blit de **COPIA** (canal A) es **~1000×**
más lento de lo esperado. El culpable es el **uso/parámetros del blit de copia** (no el recuento de
palabras: 1 columna ya cuesta ~2.5M de media). Con `dmod=0` y 1 columna vuelve a ~43 fps → confirma
que es el blit de copia, no el algoritmo. Ver `src/main.cpp` (cabecera).

## Algoritmo objetivo (pendiente de implementar bien)

Receta de la fuente/Grok: **emit-once** + reparto por frames:
- `POS` (2 px): cada 4 frames (Blitter).
- **DATA** (columna nueva): cada 32 frames (Blitter, por fracciones, NUNCA el bloque entero de golpe).
- 4 copperlists (2 pares con doble buffer) + doble buffer de estructuras DMA; swap en VBlank.

Ver `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md` y la consulta
`docs/debugging/investigaciones/consulta-freeform-scroll-blitter-en.md`.

## Lanzar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/212_free_scroll_layer --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/212_free_scroll_layer
```
