# INTENT — 088_blit3d_planes

## Qué hace

Paso A de la vía **por plano** de la librería 3D (la adecuada para juegos tipo Superhot/Castle Master): un cubo isométrico estático con tres caras visibles y cuatro colores sobre **dos bitplanes**. El relleno se hace con una pasada por plano (`fill_polygons_by_plane`), que es el algoritmo documentado en `polygon_planes.hpp`: cada cara aporta su contorno a los planos de su máscara de color y las aristas compartidas por caras del mismo bit se cancelan; la magenta (bit 3 = planos 0 y 1) se rellena en **ambas** pasadas.

## Qué pretende demostrar

- Que el **color compuesto por plano** funciona: una cara azul (plano 0), una roja (plano 1) y una magenta (planos 0 y 1) conviven con dos fills por frame.
- Que el pipeline de escena + `PlaneFillBuilder` emite las caras sin triangular y sin copias por cara.
- Sirve de **reproducibilidad mínima** del bug abierto del area fill por plano (filas con cruce izquierdo inclinado sin rellenar).

## Elementos en pantalla

- **Cubo isométrico** centrado: cara izquierda azul (1), cara superior roja (2), cara derecha magenta (3); fondo negro (0).
- **Franja inferior** (filas 232..255): `088 BLIT3D PLANES`, versión con build y contador `F<num>` en magenta.

## Evolución temporal

- Escena estática (solo avanza el contador de frames). El siguiente paso (rotación 3D con culling) se añadirá sobre esta misma demo.
- **Estado: PASO A EN CURSO.** Los colores son correctos, pero el area fill por plano deja sin rellenar las filas cuyo cruce izquierdo está en una arista inclinada (cara superior). Detalle y siguiente experimento en `VALIDATION.md`.
