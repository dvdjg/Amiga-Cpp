# 203 - Relleno de polígonos compuesto por bitplane

Motor poligonal 2D/3D mínimo que usa el **relleno compuesto por bitplane**: en vez de rellenar
polígono a polígono (un `area fill` por cara), se rellena **por plano** (un `area fill` por
bitplane).

## Técnica

El Blitter rellena **máscaras de 1 bit** y el color de una cara es un patrón de bits en los
planos. Por eso, para cada bitplane `p`:

1. se limpia el plano `p`;
2. se dibuja el contorno XOR (ONEDOT) de las caras cuyo color tiene el bit `p` a 1;
3. **un solo `area fill`** (`FILL_XOR`) rellena todas las regiones de ese plano.

Las **aristas compartidas** por dos caras del mismo bit se dibujan dos veces y el doble cruce
even-odd las cancela (las dos caras se funden en una región); donde los bits difieren, la arista
es frontera real y separa dos colores. Así, `N` caras con `P` planos cuestan `P` fills en vez de
`N`.

```
  caras (color 1..6)            plano 0 (bit0)            plano 1 (bit1)         ...
  +----+----+                   contorno XOR de           contorno XOR de
  | A  | B  |   --clasificar--> caras con bit0=1   -->    caras con bit1=1
  |    |    |                   + 1 area fill            + 1 area fill
  +----+----+                   = regiones del plano 0   = regiones del plano 1
```

## Recorrido

Un cubo gira; cada cara visible se sombrea por profundidad (`shade_of`) y se acumula en un
`eng::graphics::PlaneFillBuilder` (patrón `SubmitPoly`/`EndFrame`). Al cerrar el frame se vuelca
con `MinimalBackend::fill_polygons_by_plane` (camino Blitter, verificado en hardware). La
referencia CPU del mismo algoritmo es `fill_polygons_by_plane_cpu` (test **HOST-217**).

Sobre la banda inferior se OR-ado un **suelo texturizado** con `graphics::add_rect_pattern`
(patrón planar **multifila**: un `PatternFill` por fila del patrón, camino `OrBlob`), que valida
ese relleno en hardware.

- Display 256×256×4 (mismos registros que el port de `flatshade-convex`), doble buffer por swap
  de copperlist (`Scene::commit`).
- El `area fill` se acota al plano (`blitter_area_fill_rect`); el barrido de 1024 filas de
  `blitter_area_fill` está pensado para el bitmap multicapa contiguo de 116.

## Verificación

- `analyze-screenshot.sh`: fondo, sólido relleno presente.
- `analyze-sequence.sh`: el sólido cambia entre frames (gira) y usa **≥2 colores de cara**.

```bash
bash ./tools/test-regression.sh --demo demos/amiga/203_polygon_planes
```
