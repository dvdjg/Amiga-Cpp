# HOST-482: recorte de caras al bitmap en el relleno por plano

Test host del recorte que hace segura la envolvente del Blitter en el camino por plano (`eng::graphics/polygon_planes.hpp`).

## Qué comprueba

`clip_face_to_rect` recorta un polígono convexo al rectángulo `[min_x, max_x] × [min_y, max_y]` (Sutherland–Hodgman entero, una división por arista cortada) y `plan_plane_edges_in_bounds` planifica el contorno de las caras ya recortadas, con la misma dedupe de paridad que `plan_plane_edges`.

1. **Interior intacto**: una cara dentro del rectángulo sale con los mismos vértices (sin divisiones ni cambios).
2. **Caso del bug (`x == width`)**: un vértice en 320 (un píxel fuera, como deja el clip de cámara) se recorta y el plan no emite ninguna arista en `x >= width`.
3. **Cuatro bordes**: izquierda, derecha, arriba y abajo; salida dentro del rectángulo y los vértices de corte sobre las aristas de la entrada (hull ±1 px).
4. **Fuera del todo**: 0 vértices.
5. **Arista compartida**: dos caras del mismo plano que comparten arista siguen aportándola idéntica tras el recorte, y la dedupe la elimina (multiplicidad par).
6. **Plantilla**: el mismo recorte instanciado con `s32` además de `s16`.

El margen izquierdo es **1** (el camino del Blitter lo pasa así) porque el trazador de líneas escribe un paso por detrás: una vertical dada en `x` se dibuja en `x-1` y `x=0` acabaría en la cola de la fila anterior. Ver [`blitter-line-subpixel-fill.md` §2.1](../../../../docs/reference/amiga/techniques/blitter-line-subpixel-fill.md) y el test on-target `L1-003`.

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/graphics/482_plane_fill_in_bounds
```

Salida esperada:

```
OK: recorte de caras al bitmap en el relleno por plano
```
