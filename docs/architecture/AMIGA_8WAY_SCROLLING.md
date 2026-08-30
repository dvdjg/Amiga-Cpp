# Scroll 8-way circular en Amiga

La técnica sigue el patrón de Aminet ScrollingTrick y la explicación de GitHub
Part 12: el display avanza un píxel por frame, mientras una superficie física
contiene dos displays y dos bloques de margen:

```text
X: 2*display_width  + 2*BLOCKWIDTH
Y: 2*display_height + 2*BLOCKHEIGHT
```

No hay tres páginas lineales. La cámara puede moverse hacia delante, atrás o en
diagonal. Solo cuando una frontera de `BLOCKWIDTH/BLOCKHEIGHT` entra en la
ventana se generan las bandas necesarias.

En X se dibuja una columna de bloque a lo largo de la superficie. En Y se dibuja
una fila de bloque. En XY se generan ambas, pero la celda de esquina pertenece
a la primera banda y se elimina de la segunda. El número mínimo de trabajos
lógicos es 0, 1 o 2 según los ejes que cruzaron un bloque, nunca una página
completa ni dos veces la esquina.

El destino se comprueba fuera del rectángulo visible. Un job pendiente se
consume gradualmente en pasos de tiles; el Blitter es el único que copia píxeles.
El mapa se consulta justo antes de crear el job, de modo que una inversión de
sentido no deja una cola vieja escribiendo contenido incorrecto.

Al alcanzar el margen, el controlador cambia la posición física de la ventana
al lado coincidente, ajusta `surface_origin_*` y actualiza los punteros del
display. Los dos lados ya preparados representan el mismo tramo lógico; por
eso el recentrado no necesita mover toda la pantalla. La operación es válida en
ambos sentidos y por eje.

Para un viewport de 320 píxeles, `DDFSTRT=$30` obtiene 42 bytes por fila: 40
visibles y 2 de margen. El puntero coarse usa `(scroll-1)&~15` y el fine scroll
usa `BPLCON1=(16-fine)&15`; esta combinación mantiene continuidad al pasar de
fine 15 a 0. Si aparece ruido en los primeros 16 píxeles, se puede ocultar esa
banda en la composición, nunca rellenarla desde la CPU.

El backend comienza una operación del Blitter por plano. La API portable solo
describe un `TileBlockCopy` lógico con `bitplane_count`; no simula una operación
multi-plano que el OCS no ofrece.

Con solo X, la superficie es `(2*VW+2*BW) x VH`; con solo Y es
`VW x (2*VH+2*BH)`. Esto evita reservar margen inútil. Los tiles deben tener
anchura múltiplo de 16; un mundo no periódico termina en `edge_tile` y un mapa
con `wrap_x/wrap_y` permite scroll infinito lógico.
