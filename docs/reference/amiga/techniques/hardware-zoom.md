# Zoom hardware (BPLxMOD vertical + BPLCON1 por línea)

**Qué resuelve**: *zoom* (y “respiración” de logos/fondos) en Amiga **sin hardware de transformación**, con el **Copper** haciendo el trabajo. No es una técnica única: los zooms potentes de la demoscene mezclan varias.

| Técnica | Cómo funciona | Calidad/límite | Uso |
|---|---|---|---|
| **Vertical por `BPLxMOD`** | el Copper cambia el **módulo** de los bitplanes por línea → repite o salta líneas de la imagen | gratis en CPU; vertical puro | zoom vertical, *stretch*, “respiración” |
| **Horizontal por `BPLCON1`** (truco `$102`) | cambio de `BPLCON1` **a mitad de línea** (scroll fino 0–15) → esconde los últimos píxeles de cada grupo de 16 | timing crítico; **solo hasta 4 planos** | compresión horizontal ligera |
| **Pre-escalados + Copper** | N versiones ya escaladas; el Copper elige versión + ajusta con módulos | excelente, gasta memoria | zooms grandes y limpios |
| **Blitter** | copia línea a línea con anchos/alturas distintas | flexible, consume Blitter | zoom de objetos (no pantalla completa) |
| **Rotozoomer** | pre-cálculo + Blitter/CPU (+ C2P) | espectacular | efecto de demo |

## Zoom vertical por módulos

Después de leer cada línea, el hardware añade **el ancho de la línea + `BPLxMOD`**. Dos formas de controlarlo:

- escribir `BPLxPTH/L` al inicio de cada línea (más caro de parchear), o
- escribir **solo `BPL1MOD`/`BPL2MOD`** (2 valores por línea, mucho más barato).

Se construye **una vez** una copperlist fija de 256 `WAIT` + `MOVE` a los módulos, y por frame **se parchean solo los valores** según el factor de zoom. Tabla de módulos (fixed-point):

```text
accum += zoom_fp;                // zoom_fp 8.8: 256 = 1:1, >256 acerca, <256 aleja
advance   = accum >> 8;          // líneas del source que avanza esta línea
accum    &= 0xFF;
mod[y]    = (advance - 1) * bytes_por_linea;   // 1 → 0 (normal); 2 → +stride; 0 → repite
```

**Centrado**: al esconder líneas la imagen se “comprime” hacia arriba; se recentra con `DIWSTRT`/`DIWSTOP` o moviendo el punto de inicio de los punteros (el módulo actúa **después** de dibujar la línea).

## Zoom horizontal: el truco `$102`

`BPLCON1` controla el scroll fino (0–15 px). **Cambiándolo a mitad de línea** se “esconden” los últimos píxeles de los grupos de 16 → compresión horizontal. La **magic table** da, para cada número de píxeles a ocultar (1–15), **qué valor** escribir en `BPLCON1` y **en qué posición horizontal** de la línea hacerlo. Dos consecuencias del artículo:

- funciona bien **solo hasta 4 bitplanes**;
- hay que **compensar** el desplazamiento a la izquierda (mover el origen con punteros/scroll).

Los valores exactos de la magic table y las posiciones están en el artículo (y en su ZIP de fuentes `zoom0.s`…`zoom4.s`, ver *Enlaces*); aquí se documenta el **patrón**, no una tabla autoritativa. Cuando se agota el rango de `BPLCON1`, se combina con **Blitter** o **pre-escalados**.

## En el engine

**Se reutiliza:**

| Pieza | Dónde |
|---|---|
| Repetir/saltar líneas con `BPLxMOD` (ya encapsulado) | `compose.hpp::row_repeat` (`:893-908`) |
| Escribir un registro a mitad de línea (WAIT con posición horizontal) | `Scheduler::wait_position`/`wait_position_safe` (`scheduler.hpp:242/251`) |
| Parcheo por frame de un `MOVE` (1 store) | `copper::PatchHandle` (`scheduler.hpp:72-85`, HOST-214) |
| Scroll fino (`BPLCON1`) y DDF | `effects::FineScroll` (`api/effects.hpp:309`), `graphics/playfield_scroll.hpp:20-32` |
| Doble buffer de copperlist | `copper::DoubleBuffer` (`double_buffer.hpp:49`) |
| **Rotozoom por píxel** (CPU + C2P) ya implementado | `graphics/effects/rotozoom.hpp` → `effects::Rotozoom` (`api/effects.hpp:115`); ASM `support/rotozoom_loop.s`; HOST-132 |

**Falta**: la **tabla de módulos por línea** (más allá de `row_repeat`, que es una sola pasada), el **truco `$102`** (magic table + escritura de `BPLCON1` a mitad de línea) y los backends de **pre-escalados** y **Blitter zoom**.

## Trampas

- **El módulo actúa tras la línea**: el recentrado va por `DIWSTRT`/punteros, no por el módulo.
- El truco horizontal exige **timing exacto** (posición horizontal del `WAIT`) y **≤4 planos**.
- Zoom vertical y horizontal se **combinan** (módulos + `BPLCON1`) pero cada uno tiene su coste de Copper; ver [`copper-timing-and-budget.md`](copper-timing-and-budget.md).
- El rotozoom “clásico” (chunky) **no** es esto: ese pasa por CPU/C2P; el zoom hardware escala el planar en el vuelo del haz.

## Enlaces

- **Obligement — «Zoom matériel avec BPLXMOD et BPLCON1»** (Yragael, 2018; el análisis más completo, con fuentes `zoom0.s`…`zoom4.s` y figura de la limitación a 4 planos): <http://obligement.free.fr/articles/assembleur_zoom_materiel_bplxmod_bplcon1.php> · ZIP de código: <http://obligement.free.fr/files/code_zoom_materiel.zip>
- **Original en Stash of Code**: <https://www.stashofcode.fr/zoom-hardware-avec-bplxmod-et-bplcon1-sur-amiga/>
- **Demo de referencia**: *World of Commodore 92* (Sanity) → <https://www.pouet.net/prod.php?which=2938>
- Imagen de ejemplo: *Dragon Sun* (Cougar/Sanity) → <http://amiga.lychesis.net/scene/Cougar/Cougar_DragonSun32.html>
- Módulo para leer el artículo: *Helmet For Sale* (Jason/Kefrens) → <http://janeway.exotica.org.uk/release.php?id=8261>
- Artículo hermano del mismo autor (scroll sinusoidal): <http://obligement.free.fr/articles/assembleur_programmer_defilement_sinusoidal_1.php>
- Contexto histórico: **Mode 7** de SNES → <https://en.wikipedia.org/wiki/Mode_7>
- **Aportado por el usuario** (esquema de módulos, magic table simplificada e integración con la carretera); el plan está en [`ROADMAP_HW_ZOOM.md`](../../guides/roadmap/ROADMAP_HW_ZOOM.md).

## Referencias

- Fichas locales: [`modulo-tricks.md`](modulo-tricks.md), [`copper-timing-and-budget.md`](copper-timing-and-budget.md), [`copper-road-rasters.md`](copper-road-rasters.md).
- AHRM 3.ª: `BPLCON1`, `BPL1MOD`/`BPL2MOD`, `DIWSTRT`/`DIWSTOP` (`docs/reference/ahrm/`).
- Plan por fases: [`ROADMAP_HW_ZOOM.md`](../../guides/roadmap/ROADMAP_HW_ZOOM.md).
