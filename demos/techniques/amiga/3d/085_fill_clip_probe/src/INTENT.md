# INTENT — 085_fill_clip_probe (sonda de recorte 2D + relleno por plano)

## Qué hace

Sonda **mínima y aislada** del camino «recorte 2D + relleno por plano» que usan las demos 3D (`retro::clip_polygon` + `fill_polygons_by_plane` por Blitter), siguiendo el protocolo de etapas (`PROTOCOLO_ETAPAS_GRAFICOS.md`): pocos elementos, colores planos, caso límite explícito. Dibuja sobre 4 planos estándar 320×256 (los planos 0-1 rellenos; 2-3 en negro):

- un **triángulo** que barre horizontalmente y cruza el borde derecho (parcialmente fuera casi todo el ciclo),
- un **pentágono** fijo medio fuera por abajo-izquierda (parcial constante),
- un **cuadrado** de referencia siempre dentro.

Ablaciones de compilación: `-DCLIP_PROBE_CPU` (misma escena por la referencia CPU `fill_polygons_by_plane_cpu`, para A/B píxel a píxel) y `-DCLIP_PROBE_NO_CLIP` (sin recortar).

## Qué pretende demostrar

1. **Recorte parcial correcto**: un polígono con parte visible dentro se recorta conservando exactamente los vértices interiores (regresión del bug de `clip_polygon_pass`, que perdía el cruce de la arista de cierre: HOST-479).
2. **Convención de bordes del recorte**: con la semántica semiabierta de `clip_inside` (`x < maxX`), el rect de recorte debe usar la **última columna/fila incluida** (`width-1`/`height-1`): pasar `width`/`height` deja los vértices de corte FUERA del buffer y el contorno del Blitter pinta en la palabra vecina (corrupción).
3. **Equivalencia CPU ↔ Blitter** del relleno con recorte parcial (A/B con `-DCLIP_PROBE_CPU`).

## Elementos en pantalla

| Elemento | Color | Papel |
|---|---|---|
| Triángulo grande | rojo (índice 1) | barre de dentro a mitad fuera por la derecha (el caso del recorte) |
| Pentágono | verde (índice 2) | fijo, medio fuera por abajo-izquierda |
| Cuadrado | blanco (índice 3) | referencia siempre dentro |

## Evolución temporal

El triángulo avanza 2 px por frame (barrido de 300 px en 150 frames): entra, cruza el borde y vuelve a entrar. Los otros dos son estáticos.

## Estado (WIP)

La sonda **está en desarrollo** (iteraciones del protocolo): funciona, dibuja los tres elementos y ya ha servido para encontrar y corregir el bug de cierre del recorte (HOST-479) y documentar la convención de bordes. Quedan artefactos en el A/B (bandas/rayado cruzando el relleno) cuyo aislamiento sigue el ciclo: comparación píxel a píxel CPU vs Blitter sobre frames idénticos de las dos variantes.
