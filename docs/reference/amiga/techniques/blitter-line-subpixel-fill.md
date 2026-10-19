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

## 2.1 El trazador escribe un paso por detrás: rectángulo seguro del contorno

El lazo de línea de WinUAE (ver §1) dibuja el píxel con el puntero de C **del paso anterior** y el `BLTDPTR` inicial solo se usa para el primer píxel; después `BLTDPTR := BLTCPTR` en cada iteración (WinUAE-DBG `blitter.cpp:929-969`, `blitter_line_write` en `:704-710`). El efecto neto, medido con la sonda estática del repo (`tests/amiga/l1_backend/003_plane_fill_bounds`, modo “solo contornos”):

- una **vertical** dada en `x` se dibuja en **`x−1`** y sus filas empiezan una fila más abajo (`y0+1`);
- el **primer píxel** de cada línea va por el canal **D** y se pierde de la línea. Con `D = base del plano` la palabra modificada aterriza en la **fila 0** del bitmap; el truco clásico de `BLTDPTR = base` solo es inocuo si la base queda fuera de la ventana mostrada, y en un bitmap que es exactamente la ventana (el caso del engine) deja píxeles sueltos en la esquina. La solución del engine es una **palabra de descarte de 2 B en Chip** (`m_line_trash` del backend) pasada como `d_base`: se machaca en cada línea y no la lee nadie.
- el **area fill** `FILL_XOR` trabaja sobre el contorno *dibujado*: para las aristas izquierda/derecha en `xl`/`xr` deja el tramo `[xl+1, xr]`. Como el contorno se dibuja un píxel a la izquierda, el **resultado relleno** coincide con el polígono pedido en `x` (p. ej. una cara `[40,120]` sale rellena en `[40,119]`, la convención `[x0, x1)` del relleno CPU), con una fila de desfase en `y`.

**Regla**: las coordenadas que se entregan al trazador deben caer en el **rectángulo seguro** `[1, width−1] × [0, height−1]` (píxeles incluidos): el margen izquierdo de 1 px absorbe el desfase, el último píxel incluido se dibuja en `width−2` (dentro) y el contorno queda **cerrado** en el borde, que es lo que el `FILL_XOR` necesita para la paridad par dentro de la caja. Con `x = 0` la vertical se dibuja en `x = −1` y `(u32(-1) >> 3) & ~1` cae, por el complemento a dos, en la **cola de la fila anterior** (`x≈width−1`): el contorno queda fuera del bbox de la cara y el clear por rectángulo sucio no lo borra nunca (residuo que reaparece cuando el objeto se aleja del borde; era el artefacto de la demo 088). Con `x = width` ocurre lo simétrico (`x` cae en la primera palabra de la fila siguiente).

En el engine lo aplica `eng::graphics::plan_plane_edges_in_bounds` (`polygon_planes.hpp`), que recorta cada cara con `clip_face_to_rect` antes de planificar el contorno; el camino del Blitter pasa `min_x = 1`, `max_x = width−1`, `max_y = height−1`. La cobertura está en `HOST-482` (host) y `L1-003` (on-target, con caras en `x=320`, `x=-1` e `y=232` y comparación contra el relleno CPU).

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

## 5. Relleno multi-plano: aceleración y patrones (análisis medido)

`AmigaBackend::fill_polygons_by_plane` ya aplica la optimización estructural de la técnica (bloque «por PLANO» del encabezado de `polygon_planes.hpp`): **1 clear + aristas + 1 fill por plano**, con las aristas compartidas por caras del mismo bit cancelándose por paridad. Sobre ese punto de partida, las opciones evaluadas para acelerar el relleno cuando se usan varios bitplanes (coste por palabra: A/B/C leídos = 2 ciclos cada uno, D escrito = 2; `FILL_XOR` añade 1 ciclo de espera si D va sin C — WinUAE-DBG `blitter.cpp:180-192`):

| Opción | Coste / efecto | Decisión |
|---|---|---|
| Clear con minterm `0` (solo D) | 2 ciclos/palabra (ya implementado en `blit_clear_region`) | Ya en uso |
| Cajas de relleno por grupo de planos (el llamador recorta filas/x) | Proporcional al área usada; la 080 ya recorta sala (filas 8..200) y paneles (52..104) | Ya en uso |
| Barrido único de N planos contiguos (clear y fill de `N·filas` de una pasada, como `BitmapFillFast`) | Ahorra (N−1) setups+sincronizaciones; **solo aplica si `plane_stride == filas·row_bytes`** (buffers compactos) — con framebuffers de altura completa y caja parcial, NO | Descartado aquí (documentado) |
| Fill con C = D para quitar la espera de «D sin C» | C leído cuesta 2 > 1 de espera | Descartado |
| Dedupe de aristas de paridad par (caras del mismo bit comparten arista: 2 dibujos = 0) | Ahorra blits de línea; identidad de aristas por plano (`plan_plane_edges`, host-test de equivalencia de región) | **Implementado** (engine + HOST-474) |
| **Patrón de relleno** (`fill_pattern.hpp`): tras el fill, un blit `D = A & B` con `B = D` y A = plano de patrón 1-bit | +1 blit por plano (6 ciclos/palabra) sobre la misma caja; el plano de patrón se construye una vez | **Implementado** (HOST-474; demo 080) |
| Aplicación de patrón **por fases de fila** (`pattern_period`, filas uniformes del catálogo): P blits con `DMOD = P·row_bytes − 2·words`, A deshabilitado (`BLTADAT` = palabra de la fase) | Fase `0xFFFF` pasa sin blit; `0x0000` → clear D-only (2 c/palabra); resto → 4 c/palabra (solo B leído + D escrito). Requiere filas uniformes (el catálogo lo es) | **Implementado**: `fill` mínimo de la 080 baja 117k→104k |
| **Patrón por plano** (`fill_polygons_by_plane_patterns`, `fill_dither_plane_cpu`): damero 1×1 y su inverso repartidos por plano | Mezcla dos índices base en píxeles alternos (colores adicionales sin tocar la paleta); 2 filas de patrón por plano (periodo 2) | **Implementado**: demo 080 (sala con mezclas 0/1, 0/2 y 1/2), HOST-474 |

El area fill del Blitter **no puede** producir por sí mismo un patrón (solo escribe 1s OR/XOR en el interior; semántica exacta en WinUAE-DBG `blitter.cpp:262-286` `build_blitfilltable`), así que el patrón es necesariamente un segundo paso `D = A & B` (minterm `$C0`, B realimentado con el destino), sin tocar el contorno ni la paridad. Catálogo actual: tablero 2×2/4×4, punteado, líneas verticales/horizontales y oblicuas 45°/135° (palabras de 16 px, MSB = píxel izquierdo, fase horizontal de 16 px).

Cotejo con consulta externa (Grok, 2026-10): coincide en que el area fill no puede aplicar patrón y propone «relleno sólido + cookie-cut» (`D = (A & pattern) | (~A & D)` con A = máscara). Nuestra forma es la variante barata de esa receta: el plano **ya** contiene el relleno sólido (la máscara), así que el paso de patrón no necesita máscara aparte ni C realimentado: `D = A & B` con `B = D` = 6 c/palabra (la cookie-cut con máscara explícita lee A+B+C+D = 8). Su idea del patrón en `BLTADAT` para patrones de 1-2 palabras es exactamente la ruta por fases implementada para las filas uniformes.

Medida real (demo 080, caja de sala 352×193 en 2 planos, `profile.mjs`): pasada completa 426k–552k ciclos sin patrón; 508k–595k con `Checker4` en un solo pase de plano; la aplicación por fases baja el mínimo de la sección `fill` de 117k a 104k (≈ 2 c/palabra menos por fase aplicada). El coste del patrón es, por tanto, el precio del segundo paso; el ahorro multi-plano restante está en las aristas (dedupe) y en el trabajo CPU de proyección/recorte (el grueso del Full medida la sección `fill`).
