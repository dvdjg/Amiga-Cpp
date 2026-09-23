# Trazado de líneas y rasterizado sub-píxel de polígonos con el Blitter

Ficha operativa del trazado de líneas por hardware y del **rasterizado sub-píxel** de polígonos con el Blitter de OCS/ECS. Procede del repo `blitter-subpixel-line` (dos PDF + implementaciones ASM `src/triangle.S` y `src/cube.S`). Complementa `amiga-bootcamp/08_graphics/blitter_programming.md` (Blitter general: minterms, cookie-cut, `BLTSIZE`, ejemplos de línea y area fill) con la derivación del acumulador y el caso sub-píxel. Referencia de registros: AHRM 3.ª, capítulo del Blitter, y `custom.i`/`blit.i` del repo de origen.

## 1. Modo línea (`LINEMODE`)

Con `BLTCON1` bit 0 = 1 el Blitter dibuja una línea (Bresenham por hardware) en vez de un rectángulo. Itera `count` veces; en cada iteración:

1. Actualiza el píxel de la posición actual (`OR` o `XOR` según el minterm).
2. Avanza **siempre** en la dirección **mayor**; si el **acumulador** ≥ 0, avanza **además** en la **menor**.
3. Suma al acumulador `inc_maj` (si no hubo paso menor) o `inc_majmin` (si lo hubo).

La dirección se describe por **octante** (mayor/menor):

```text
        6 7
      5     8
      4     1
        3 2

  octante  mayor  menor  codigo (BLTCON1 bits 4-2)
    1       der    aba    4
    2       aba    der    0
    3       aba    izq    2
    4       izq    aba    5
    5       izq    arr    7
    6       arr    izq    3
    7       arr    der    1
    8       der    arr    6
```

`maj_step = [RIGHT, DOWN, DOWN, LEFT, LEFT, UP, UP, RIGHT]` y `min_step = [DOWN, RIGHT, LEFT, DOWN, UP, LEFT, RIGHT, UP]` (índices 1..8).

### Valores a programar

Con `dx` = tamaño en la dirección mayor y `dy` = tamaño en la menor (valores absolutos):

- **Bresenham** (`acc0 = 2*dy - dx`): el paso menor se toma si el píxel `(x'+1, y'+1)` está más cerca de la línea que `(x'+1, y')`.
- Los registros de acumulador/incrementos solo admiten **números pares** (se truncan al par menor), así que se multiplica la inecuación por 2:

```text
  acc         = 4*dy - 2*dx
  inc_maj     = 4*dy
  inc_majmin  = 4*(dy - dx)
```

### Pseudo-código del Blitter

```text
dot_on_row = false
repetir count veces:
    si (no onedot) o (onedot y no dot_on_row):
        *p |= (0x8000 >> shift)            ; o ^= si XOR
        dot_on_row = true
    si acc < 0: step = maj_step[oct]; acc += inc_maj
    sino:       step = maj_step[oct] | min_step[oct]; acc += inc_majmin
    segun step: LEFT -> shift--  (wrap 15, p -= 2)
                RIGHT-> shift++  (wrap 0,  p += 2)
                UP   -> p -= ymod; dot_on_row = false
                DOWN -> p += ymod; dot_on_row = false
```

### Mapa de registros (línea)

| Valor | Registro | Nota |
|---|---|---|
| `$8000` | `BLTADAT` | first-word mask del canal A |
| `$FFFF` | `BLTBDAT` | textura de la línea (sólida) |
| `$FFFF` | `BLTAFWM`/`BLTALWM` | sin recorte por bordes |
| `inc_majmin` | `BLTAMOD` | |
| `inc_maj` | `BLTBMOD` | |
| `ymod` | `BLTCMOD`/`BLTDMOD` | bytes por fila del framebuffer |
| `acc` | `BLTAPTR` | long con signo (4 bytes) |
| `p` | `BLTCPTR`/`BLTDPTR` | word que contiene el primer píxel |
| `shift` | `BLTCON0` bits 15-12 | `x1 mod 16` (giros de `$8000`) |
| `$C00` | `BLTCON0` bits 11-8 | canales A y B activos |
| `$42` XOR / `$F2` OR | `BLTCON0` bits 7-0 | minterm |
| `codigo` | `BLTCON1` bits 4-2 | código del octante |
| `SIGNFLAG` (`$40`) | `BLTCON1` bit 6 | si `acc < 0` |
| `ONEDOT` (`$02`) | `BLTCON1` bit 1 | solo el primer píxel por fila |
| `1` | `BLTCON1` bit 0 | activa el modo línea |
| `count` | `BLTSIZE` bits 15-6 | iteraciones del bucle |
| `2` | `BLTSIZE` bits 5-0 | se escribe el último (arranca) |

### `BLTDPTR` en modo línea (primer píxel)

En modo línea el Blitter escribe el **primer píxel** de la línea por el canal **D** y el resto por **C**. Dejar `BLTDPTR` fijo en la **base del bitmap** (en vez de la dirección calculada de la línea) descarta ese primer píxel en un punto inofensivo y mantiene el contorno con la **paridad par/impar correcta en los vértices**. Es lo que hace `DrawObject` de `flatshade-convex` (`bltdpt = planes`): con `BLTDPTR` = dirección calculada, el contorno pierde un cruce en cada vértice y el area fill `XOR` posterior **filtra una raya horizontal** en cada uno.

### `BLTSIZE` con campos a 0

El campo de **altura** (bits 15-6) vale 0 → **1024 líneas** (no 0); el de **anchura** (bits 5-0) vale 0 → **64 words**. Por eso `BitmapClearFast` y `BitmapFillFast` barren los 4 planos contiguos de 256×256 (1024 líneas × 16 words = 32768 bytes) con un único blit de `BLTSIZE` de altura 0 y anchura 16, sin ser un no-op.

## 2. Rasterizado sub-píxel de polígonos

Se hace en **dos pasos**: (1) contorno por líneas en modo `ONEDOT`; (2) **area fill**.

### Area fill

Recorre cada fila de **derecha a izquierda** con un biestable de relleno:

```text
fill = false
para x de W-1 hasta 0:
    should_fill = fill
    si pixel[x]: fill = !fill
    si should_fill: pixel[x] = !pixel[x]
```

Al encontrar un píxel encendido conmuta el relleno; si el relleno estaba activo, invierte el píxel. El resultado del contorno debe ser **el píxel inmediatamente a la izquierda de cada arista** en cada fila cubierta (no la arista exacta): así el fill rellena el interior. La primera y la última fila del polígono no llevan píxel de contorno, lo que se consigue dibujando en **modo `XOR`**.

### Derivación sub-píxel (ejemplo, octante 1)

Con la arista `v2=(x2,y2) -> v3=(x3,y3)`, `dx = x3-x2`, `dy = y3-y2`, y coordenadas de vértice en **fixed-point 12.4**:

```text
Y2 = floor(y2 + 1)          ; primera fila cubierta
Y3 = floor(y3)              ; última fila cubierta
si Y3 < Y2: no se cubre ninguna fila (se omite)
X2 = floor(x2 + (Y2 - y2)*dx/dy)   ; x del primer píxel del contorno
X3 = floor(x2 + (Y3 - y2)*dx/dy)

acc        = 2*((X2 - x2 + 2)*dy - (Y2 - y2 + 1)*dx)
inc_maj    = 2*dy
inc_majmin = 2*(dy - dx)
count      = X3 - X2 + 1
```

El resto de octantes es simétrico (intercambiar los roles de los ejes). El contorno se dibuja con `ONEDOT` (solo el primer píxel de cada fila) y minterm `XOR`, y el area fill con `FILL_XOR`.

Nota: las operaciones por `dy` necesitan **división** (`/dy`); conviene `dy != 0` (aristas horizontales no cubren filas) y aritmética de 16 bits con redondeo cuidadoso (12.4).

## 3. Receta: dibujar un polígono relleno (contorno `ONEDOT` + area fill)

Secuencia canónica por frame (la del efecto `flatshade-convex`; ver §1 para el detalle de registros de cada paso):

```text
  limpiar destino (1 blit)  ->  contorno de aristas visibles (ONEDOT+EOR)  ->  area fill XOR (1 blit)
```

1. **Limpiar** el destino con **un** blit. Para barrer planos contiguos (`p` planos de `h` filas y `w/16` words), usar `BLTSIZE` con **altura 0** (= 1024 líneas) y anchura `w/16`: `bltcon0 = DEST|A_TO_D`, `bltafwm/alwm = -1`, `bltadat = 0`, `bltdmod = 0`, `bltdpt = base`.
2. **Contorno**: por cada arista VISIBLE (color de arista `edgeColor > 0`):
   - descartar las aristas **horizontales** (`y0 == y1`);
   - programar el modo línea `ONEDOT` + minterm **EOR** (tabla de §1): `bltcon0 = rorw(x0&15,4) | BC0F_LINE_EOR`, octante en `bltcon1`, `bltamod = derr-dmax`, `bltbmod = dmin<<1`, `bltapt = derr`, `bltsize = (dmax<<6)+66`;
   - **`BLTDPTR` = base del bitmap** (NO la dirección calculada de la línea); `BLTCPTR` = dirección calculada;
   - replicar en cada plano con el bit puesto en `edgeColor`, recorriendo el plano destino con `bltcm`/`bltdm` = bytes por fila y avanzando la dirección de plano (`+= plane_bytes`);
   - limpiar el flag de la arista tras dibujarla (la visibilidad se reconstruye cada frame).
3. **Relleno**: **un** `blitter_area_fill` sobre los planos contiguos: `bltcon0 = (SRCA|DEST)|A_TO_D`, `bltcon1 = BLITREVERSE|FILL_XOR`, `bltamod = bltdmod = 0`, semilla = **última palabra del último plano**, `BLTSIZE` con **altura 0** y anchura `w/16`.

**Invariantes (si falla una, sale una raya horizontal por vértice):**

- Cada scanline debe cruzar el contorno un número **impar** de veces dentro del objeto (relleno *even-odd*). El contorno cerrado + `FILL_XOR` con `FCI=0` garantiza relleno dentro y vacío fuera.
- **`BLTDPTR` = base del bitmap.** En modo línea el primer píxel va por el canal **D**; si `BLTDPTR` = dirección calculada, el primer píxel (el **vértice**) se escribe por D y por C y se cancela con EOR (`1 XOR 1 = 0`) → paridad rota → raya. Con `BLTDPTR` en la base, la escritura de D cae fuera y el vértice lo escribe solo C.
- **Sin aristas horizontales** (`y0 == y1`): no aportan contorno y meten cruces falsos.
- El contorno y el fill deben operar sobre **los mismos planos contiguos**; el fill barre los `p` planos de una pasada (`256×256` → `1024` líneas × 16 words).
- El relleno **no** es un no-op con altura 0: `BLTSIZE` altura 0 = 1024 líneas (§1).

**Coste**: `1` clear + `n_aristas_visibles × planos_con_bit` blits de línea + `1` fill. Sin máscara 1-bit ni cookie-cut (frente a la ruta por cara, que paga clear+contorno+fill+cookie-cut por polígono).

**`FILL_XOR` (exclusivo) vs `FILL_OR` (inclusivo)**: `FILL_XOR` es el relleno even-odd del contorno `EOR` (deja el borde izquierdo de cada fila "abierto", 1 px más estrecho); es la ruta rápida y fiel. `FILL_OR` inclusivo se usa cuando el contorno está en una **máscara** separada que luego se recorta (cookie-cut) a cada plano — la ruta por cara (`blitter_fill_polygon`), más blits.

**Qué originales usan el truco de `BLTDPTR`** (útil al importar): `flatshade-convex` (`bltdpt = planes`), `flatshade` (`bltdpt = scrbpl[0]`) y `stencil3d` (`bltdpt = planes[DEPTH]`). El `wireframe` **no** lo usa (`bltcpt = bltdpt = data`), por lo que su port no lo necesita.

## 4. Relación con el engine

El backend ya implementa:

- `AmigaBackend::blitter_line` — modo línea con minterm **OR** (`BC0F_LINE_OR`), port de `DrawObject` de `wireframe`.
- `AmigaBackend::blitter_line_eor` — modo línea `ONEDOT` + minterm **EOR** (`BC0F_LINE_EOR`), port de `DrawObject` de `flatshade-convex`; acepta `d_base` para aplicar el truco de `BLTDPTR` (ver §1).
- `AmigaBackend::blitter_area_fill` — **area fill exclusivo** (`FILL_XOR` + `BLITREVERSE`, altura 0), port de `BitmapFillFast` de `flatshade-convex`.
- `blit_line` (interno) — modo línea `ONEDOT` + minterm XOR, usado para contornos.
- `blit_fill_region` — **area fill inclusivo** (`FILL_OR` + `BLITREVERSE`), port de `BlitterFillArea` de libblit.
- `fill_triangles_blitter` — contorno + area fill + cookie-cut por plano (demo 078).

**Pendiente / no portado**: el trazado **sub-píxel** (acumulador con parte fraccionaria 12.4 y la fórmula `acc = 2*((X2-x2+2)*dy - (Y2-y2+1)*dx)`) y el contorno `ONEDOT` de polígonos sub-píxel exactos. `blit_fill_region` usa `FILL_OR` (inclusivo), no `FILL_XOR`.

**Usos actuales**: demos `079_wireframe` (línea OR) y `116_flatshade_convex` (contorno `ONEDOT`+EOR de las aristas visibles + un único `blitter_area_fill` `XOR`, con el truco de `BLTDPTR`). Un futuro rasterizador sub-píxel permitiría aristas y polígonos sin "snap" a píxel (p. ej. el `flatshade` y el `stencil3d` del repo de origen).
