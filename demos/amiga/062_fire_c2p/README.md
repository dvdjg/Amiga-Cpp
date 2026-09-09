# Demo 062: fuego 32 colores + c2p (port de effects/fire-rgb)

Importa el efecto `05-fire-rgb` de `demoscene-repo-orig` adaptado a la nueva
estructura del engine. **Fiel al original** en geometría y algorítmica: fuego a
80×64 mostrado a 320×256 (escalado 4×4 pixelado).

## Ficha técnica del original (análisis exhaustivo)

### 1. Algorítmica del fuego

El efecto clásico de fuego "por difusión" (fire effect). El original:

- Mantiene un buffer de trabajo `fire` de `u16[WIDTH*HEIGHT]` (80×64), valores
  0..252 (múltiplos de 4).
- Cada frame randomiza las **2 filas inferiores** (`RandomizeBottom`) con
  `(rand & 0x3F) * 4` (chispas).
- Recorre de **arriba a abajo** y calcula cada píxel como el promedio de 4 vecinos
  de ABAJO:

  ```
  fire[y][x] = (fire[y+2][x] + fire[y+1][x-1] + fire[y+1][x+1] + fire[y+1][x]) / 4
  ```

  Al recorrer de arriba a abajo, los vecinos de abajo son los del frame anterior
  (doble buffer **implícito**, sin copiar el buffer entero).

### 2. Color y escalado: la LUT `dualtab`

El valor de fuego (promedio 0..252) NO es un color directo. Se indexa en una LUT
`dualtab[256]` de `uint32_t`, donde cada entrada son **4 bytes = 4 píxeles chunky
consecutivos** que codifican:

- El color HAM del píxel (una ristra de 4 píxeles con las transiciones HAM
  precalculadas, porque HAM solo cambia un componente RGB por píxel).
- El **escalado horizontal 4×** (cada valor de fuego de 80 de ancho se expande a
  4 píxeles → 320 de ancho).

Es decir: la LUT combina color + escalado horizontal en una sola indirección.

### 3. c2p por Blitter (12 fases)

`ChunkyToPlanar` convierte el chunky a planar **por Blitter**, en 12 fases
(swap 8×4 dos pasadas + swap 4×4), encadenadas por la interrupción de Blitter
(`INTB_BLIT`): una fase por frame, sin esperar el Blitter de forma síncrona.

### 4. Modo de video y escalado vertical

- `SetupPlayfield(MODE_HAM, ...)`: HAM mode (más colores).
- **Line-quadrupling vertical**: en `MakeCopperList`, para cada línea de 0..HEIGHT*4
  se programa `BPL1MOD/BPL2MOD = -40` cuando `(i & 3) != 3`, de modo que cada línea
  del bitmap (64) se muestra 4 veces (→ 256).

### 5. Optimizaciones

- Procesa 2 píxeles a la vez (`u32` = 2×`u16`), con suma de `u32` (sin carry porque
  4×252 < 65536).
- Punteros en registros (`asm("a0")`, etc.) para el hot loop.
- `fastrand` en asm inline (un LCG/xorshift compacto).
- Doble buffer de `chunky` y de `screen`; triple implicado con el buffer `fire`.

## Adaptación a este engine (qué cambia y por qué es equivalente)

| Original | Esta demo | Equivalencia |
|---|---|---|
| HAM mode + `dualtab` (color+escalado) | 32 colores (5 bitplanes) + paleta directa | degradado suave, sin artefactos HAM |
| c2p por Blitter (12 fases) | `c2p_1x1_naive` de 5 planos (CPU) | mismo resultado planar; para 80×64 es rápido |
| Line-quadrupling (`BPLMOD`) | `scale4x` vertical (CPU) | misma imagen |
| Escalado horizontal 4× (en `dualtab`) | `scale4x` horizontal con LUT `kExpand4` (pixel-doubling) | misma imagen |

`kExpand4` es una LUT constexpr de 256 `u32` que expande cada bit de un byte a
4 bits (pixel-doubling 4×), reproduciendo el escalado horizontal del `dualtab`.
**Lección**: el escalado horizontal NO es replicar el byte (eso produce columnas
verticales espurias), sino expandir cada bit 4×.

## Build & run

```bash
tools/build/build-demo.sh demos/amiga/062_fire_c2p --clean
tools/run/run-demo.sh       demos/amiga/062_fire_c2p --warp
```

## Criterio de aceptación

- Compila y llega a `Ready`.
- Captura: llamas pixeladas (bloques 4×4) con degradado suave rojo→amarillo,
  subiendo desde abajo, ocupando todo el ancho, sin líneas verticales.
