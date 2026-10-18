# HOST-479: recorte 2D parcial de polígonos (`eng/retro/lib2d.hpp`)

Test host de regresión del bug de `clip_polygon_pass`: recorría **n−1 aristas** (faltaba la de cierre n−1 → 0) y lo parcheaba metiendo un vértice duplicado al final, de modo que un polígono con el **primer vértice FUERA** perdía el cruce de cierre y devolvía polígonos incompletos o **< 3 vértices con parte visible dentro** (encontrado con la sonda 085 y la revisión del usuario).

## Qué comprueba

1. **Salida válida**: ≥ 3 vértices si el polígono tiene área visible; 0 si está enteramente fuera; nunca más de n+4 (cota multi-pasada de los buffers del llamador).
2. **Relleno equivalente**: el relleno CPU del recortado contra el del original (∩ caja) con **tolerancia ±1 px en ambas direcciones** — `got ⊆ dilata(ref)` (nada inventado) y `ref ⊆ dilata(got)` (nada perdido más allá del redondeo entero del punto de corte).
3. **Hull**: todo vértice de salida cae dentro del rango de la entrada (±1 px) — el recorte solo elimina vértices o los sustituye por puntos de cruce **sobre las aristas**. Un tramo de arista > s16 calculado en 16 bits envuelve y produce vértices basura fuera de ese rango (regresión DT-041; caso real de la 080: `dy=60000` daba x=222 donde el cruce real era ~340).
4. **Casos**: triángulo mitad fuera por la derecha (regresión exacta: primer vértice fuera de la arista derecha), por la izquierda, pentágono cruzando la esquina, cuadrilátero mayor que la caja (4 bordes), enteramente fuera, uno cruzando 3 lados y dos con **span > s16** (arista de cierre y arista normal) que fallan con la versión intermedia del fix de HOST-479.

## Salida de referencia

```
OK: recorte 2D parcial (interior identico, sin perder vertices visibles)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/graphics/479_clip_partial
```

## Notas

- **Convención de bordes** (documentada en `lib2d.hpp`): con la semántica semiabierta de `clip_inside` (`x < maxX`), el rect de recorte debe pasar la **última columna/fila incluida** (`width−1`/`height−1`): con `width`/`height` los vértices de corte quedan fuera del buffer y el contorno del Blitter pinta en la palabra vecina.
- El caso de la sonda (demo 085) es el escenario de uso: recorte 2D + relleno por plano del Blitter con polígonos parcialmente fuera.
