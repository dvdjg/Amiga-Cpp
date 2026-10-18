# Horizonte cielo/tierra por offset de scanline (variantes RoboCod y DPF 2+3)

**Qué resuelve**: un **horizonte móvil e inclinado** (cielo/tierra, con pendiente, yaw y pitch) **sin rellenar nada cada frame**. El Copper re-apunta por scanline el plano de horizonte para que la frontera tierra↔cielo caiga en `x_h(y)`. Hay dos variantes: la **RoboCod** (un solo playfield de 5 planos; el plano 4 es la máscara cielo/tierra y la escena conserva **15 colores**) y la **DPF 2+3** (dual playfield; la escena se queda con 7 colores y el fondo con 4 índices propios). La escena 3D poligonal vive en el resto del bitmap y encaja con la técnica de [fotogramas intermedios por paneo](camera-pan-fake-frames.md) (el horizonte se corrige «de verdad» cada frame aunque la geometría vaya paneada).

## Idea común

```text
 fcm. de la cámara: x_h(y) = x_centro + slope·(y - y0)   (píxeles de pantalla)
 para cada línea y:  el Copper escribe el puntero del plano de horizonte para
                     mostrar una ventana de 320 px cuyo corte tierra/cielo
                     cae exactamente en x_h(y)
```

La frontera se materializa con **filas patrón de 640+ px** (mitades tierra/cielo) que el Copper selecciona por línea (fila + desplazamiento coarse). El fondo no se repinta nunca: solo se mueven punteros. En las dos variantes hace falta el mismo **driver de Copper por scanline** (`WAIT` + par `BPLxPTH/L` por plano), que es la pieza que hoy falta en el engine (ver la última sección).

## Variante RoboCod (single playfield, 5 planos) — la de más color

### Modelo de planos y paleta

| Playfield | Planos | Colores | Uso |
|---|---|---|---|
| Single | 0–3 | 1–15 (+0 de fondo) | **escena 3D** (`PlaneFillBuilder` + Blitter), 15 colores |
| Single | **4** | 0 vs 16 | **máscara cielo/tierra** (1 bpp; filas de 640+ px) |

- **Paleta duplicada** (truco RoboCod, [`robocod-layered-scroll.md`](robocod-layered-scroll.md)): `palette[c] == palette[c+16]` para `c = 1..15`; `palette[0]` = cielo y `palette[16]` = tierra. La duplicación hace que la máscara **no tiña** la escena pintada: bit 4 solo decide en los píxeles de fondo (planos 0–3 = 0), donde el índice es 0 o 16.
- El **relleno por plano de la escena se llama con `planes = 4`**: limpia y pinta solo los planos 0–3 (`fill_polygons_by_plane(…, planes = 4, …)`). El plano 4 no pertenece al bitmap de la escena: es un buffer aparte que solo lee el display.
- Los polígonos usan colores 1–15 (nunca 16–31, que son las copias de paleta). El índice 0 de la escena es el «fondo»: allí se ve el horizonte.
- **Aviso de módulos**: `BPL1MOD` es el módulo de los planos **impares** (incluye BPL5). Como el puntero de BPL5 se reescribe en cada línea, el módulo no le afecta, pero debe seguir siendo válido para los planos 1 y 3.

### La emulación del scroll fino: las filas variantes

El `BPLCON1` fino es **compartido por todo el playfield** (no hay fino por plano), así que mover solo el plano 4 con punteros deja un **diente de sierra de 16 px**. La solución es mover la frontera **dentro del contenido**: la fila patrón coloca el corte en el bit exacto que haga falta.

Buffer del horizonte en Chip (una vez, al hornear):

```text
fila k (k = 0..15), orientación A (tierra | cielo), 42 words = 84 B = 672 px:
┌────────────┬──────────────────────┬─────────────────────────────┐
│ word 0     │ words 1..20          │ word 21      │ words 22..41 │
│ (guarda)   │ izquierda uniforme   │ FRONTERA     │ derecha      │
│ lado izq.  │ lado izquierdo (336px)│ corte en k   │ uniforme     │
└────────────┴──────────────────────┴──────────────┴──────────────┘
frontera en buffer px B = 336 + k     (word 21 = bytes 42..43)
```

- `k` = píxeles del **lado izquierdo** dentro de la palabra de frontera (0..15). `k = 0` → el corte está en el borde de la palabra (todo lado derecho).
- La fila lleva **672 px** (y una palabra de guarda al inicio) para que cualquier ventana con el fino compartido `f` quepa: con 640 px, los extremos `x_h = 0` y `x_h = 319` se salen cuando `f ≠ 0`.

### Las palabras de frontera: qué, cuándo y cómo se escriben

**Qué**: solo **una palabra por fila** (la central, word 21). Las mitades (words 0–20 y 22–41) se rellenan una vez con el patrón uniforme. Tabla precomputada (16 words por orientación):

```text
orientación A (izquierda = tierra = 1):   frontier_A[k] = k == 0 ? 0x0000 : 0xFFFF << (16 - k)
orientación B (izquierda = cielo = 0):    frontier_B[k] = 0xFFFF >> k
```

(`0xFFFF << (16-k)` deja los `k` bits altos a 1 = tierra y el resto a 0 = cielo; B es el complemento.)

**Cómo** (horneado): rellenar las mitades uniformes de las 16 filas y **escribir `frontier_X[k]` en la word 21 de la fila k**, para cada orientación que se use. Total: 16 (o 32) escrituras de **una palabra**. Ese es el único «scroll fino por bits»; no hay fino por plano en el hardware.

**Cuándo**: al **hornear el estilo** del horizonte (setup o carga de nivel), con el plano 4 **fuera de uso**: apagado, o escribiendo en el juego de filas inactivo (doble buffer de 1,3–2,7 KB) y conmutando después. **Nunca** mientras el haz lee la fila que se escribe (mismo principio que el tearing del fondo de la 112: una fila leída a medio escribir se ve un frame mal). **En runtime no se escribe nada**: el Copper solo parchea el par de punteros `BPL5PTH/L` por línea.
- Si el juego solo gira en yaw (horizonte **horizontal**, misma `k` en todas las líneas), con **dos filas base** basta y la escritura se hace **una vez por frame** (2 words) durante el **blanking**, antes de la primera línea del horizonte; aun así las 16 variantes horneadas son preferibles (cero escrituras y también validan el caso horizontal).
- Si el «look» del horizonte cambia en runtime (tormenta, noche, nivel nuevo), se **re-hornean las 16 palabras** de la copia inactiva en el blanking y se conmuta el juego de filas.

### Bucle por frame (RoboCod)

De la cámara salen `x_h(y)` (tabla por línea: pitch da `y0`/posición y el roll/banqueo la pendiente). Con `f` = fino de `BPLCON1` del playfield (0 si la escena no usa fino; el paneo fake lo usa):

```text
k = (x_h - f) & 15                        (variante de fila)
S = 336 + k - x_h                         (inicio de ventana en px del buffer; S ∈ [17..351])
puntero de BPL5 = fila(orientación, k) + fine_scroll_coarse(S)
```

`fine_scroll_coarse`/`fine_delay` son las convenciones del engine (`graphics/playfield_scroll.hpp`): el par (puntero, fino compartido `f`) garantiza `display_start == S`. La fórmula de `k` **absorbe el fino del paneo de la escena**, que en single playfield también mueve el plano 4. Clamp de `x_h` a `[0..319]`; fuera de ese rango la línea es toda cielo o toda tierra y se apunta a una ventana **totalmente dentro de una mitad** (words 1..20 con `S = 16` para la izquierda; words 22..41 con `S = 352` para la derecha); un horizonte horizontal exacto se resuelve entero con esas ventanas uniformes.

Emisión: plantilla emitida **una vez** (`WAIT` + 2 `MOVE` por línea, patrón «emitir con `move_at` y guardar slots» de `TileScrollScene`) y **parcheo por frame solo de los dos datos** de cada par de punteros; la fila `k` cambia cuando la frontera avanza de píxel (típicamente cada línea) y el coarse de la ventana cada 16 px de recorrido de la frontera.

### Coste y límites (RoboCod)

- **Copper**: ~6 words por línea (WAIT + 2 MOVE) → ~1,5 k words ≈ 3 KB por bloque para 256 líneas (la mitad que la variante DPF).
- **Parcheo**: 2 words de dato por línea y frame (~512 stores).
- **DMA**: 5 planos de fetch por línea, igual que la variante DPF. Sin DPF y sin `BPLCON2`.
- **Chip RAM**: 42 words × 16 filas = 1,3 KB por orientación (2,7 KB con A+B).
- **Escena**: 15 colores; el índice 0 queda compartido con cielo/tierra.
- **Sprites**: usan `COLOR16..31`, que aquí son la copia de paleta (16 = tierra, 17..31 = escena 1..15) → **sin colores propios** para sprites/HUD con sprites. En la variante DPF sí hay 16 colores dedicados.
- El mismo plano 4 **no puede** hacer a la vez de fondo RoboCod con parallax: es la máscara del horizonte.

## Variante DPF 2+3 (alternativa)

| Playfield | Planos de hardware | Colores | Uso |
|---|---|---|---|
| **PF1** (delante) | BPL1, BPL3, BPL5 (índices pares en base 0) | 0–7, con **0 transparente** | escena 3D (7 colores) |
| **PF2** (detrás) | BPL2, BPL4 (índices impares) | **8–11**, con **8 transparente** | cielo/tierra (4 índices) |

Planos pares/impares y transparencia: AHRM 3.ª (`docs/reference/ahrm/…cat.md:1990`: «color 0 in playfield 1 and color 8 in playfield 2»); `BPLCON0` con `DBLPF` (`scene/display.hpp`, `Band::bplcon0()`). `BPLCON1` en DPF tiene **un nibble por playfield** (bajo = PF1, alto = PF2): el fondo tiene scroll fino propio y **no necesita** filas variantes. Dos líneas patrón de 640 px ([tierra|cielo] y [cielo|tierra]) horneadas una vez; por línea se elige A/B y `scroll_x = 320 − x_h` (clamp a `[1..320]`), con `fine_scroll_coarse`/`fine_delay` en el nibble de PF2. La plantilla ocupa ~12 words por línea (WAIT + 2 planos × PTH/PTL + `BPLCON1`) ≈ 6 KB por bloque, que cabe en `PlanConfig::copper_bytes` (8 KB) pero compite con el resto de la lista. Los 4 índices de PF2 dan cielo, tierra y hasta 2 matices (haze, sol horneado en el patrón); el gradiente de cielo se hace moviendo `COLORxx` por bandas (patrón de la 125). En un playfield único de 5 planos el fino es compartido y el paneo de PF1 movería también el fondo; en DPF no.

## Comparación y cuándo elegir cada variante

| Criterio | RoboCod (single 5 planos) | DPF 2+3 |
|---|---|---|
| Colores de escena | **15** (+0 de fondo) | 7 |
| Cielo/tierra | 2 colores + gradientes por `COLOR` | 4 índices (8–11) |
| Sprites | sin colores propios (16..31 son copia de paleta) | **16 dedicados** |
| Fino del fondo | filas variantes horneadas (0 escrituras por frame) | nibble `BPLCON1` de PF2 |
| Copper por línea | 2 MOVE | 5 MOVE |
| DMA por línea | 5 planos | 5 planos |

**Regla práctica**: por defecto **RoboCod** (el doble de colores de escena y Copper más barato); elegir **DPF 2+3** si el juego necesita sprites con paleta propia (HUD por sprites, balas con colores dedicados) o prefiere el fino por playfield sin horneado.

## Falta de engine (compartido con la carretera por raster)

El driver genérico que emite `WAIT + par BPLxPT` en **cada scanline de una banda** todavía no existe: hoy solo hay splices puntuales (`xlimited_composer.hpp`, `emit_mode_switch_zone`) y el patrón «emitir una vez + parchear datos» de `TileScrollScene` (`graphics/drivers/tile_scroll.hpp:748-810`). El mismo driver debe servir a las tres técnicas: filas `y_source`/`x_offset` de la [carretera](copper-road-rasters.md), tabla A/B + `scroll_x(y)` del horizonte DPF, y tabla (orientación, k, S) del horizonte RoboCod. Plan: [`ROADMAP_3D_PRESENTACION.md`](../../../guides/roadmap/ROADMAP_3D_PRESENTACION.md) (fase del horizonte) y [`ROADMAP_RASTER_ROAD.md`](../../../guides/roadmap/ROADMAP_RASTER_ROAD.md) (carretera).

## Referencias

- AHRM 3.ª: dual playfield (transparencia `:1990`), `BPLCON1`/`BPL1MOD`/`BPL2MOD`, Copper (`WAIT`/`MOVE`).
- Fichas locales: [`robocod-layered-scroll.md`](robocod-layered-scroll.md) (plano 4 y paleta duplicada; guardas y tearing §3.3), [`copper-road-rasters.md`](copper-road-rasters.md), [`copper-timing-and-budget.md`](copper-timing-and-budget.md), [`dual-layer.md`](dual-layer.md).
- Engine: `engine/include/eng/graphics/playfield_scroll.hpp` (convenciones de fine/coarse), `engine/include/eng/graphics/drivers/tile_scroll.hpp` (patrón de parcheo), `engine/include/eng/graphics/copper/` (`ListBuilder`, `DoubleBuffer`), `engine/include/eng/graphics/polygon_planes.hpp` (relleno por plano con `planes = 4`).
