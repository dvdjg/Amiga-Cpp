# Intencion de la demo 096

## Qué hace

Muestra una escena de juego con una formación de rocas/props de color plano y dos naves dinámicas. Las rocas se dibujan una vez por buffer con la ruta de silueta; las naves se redibujan por frame. El HUD alterna `SIL` y `D` (objetos pintados) y hay una variante `K096_FACE=1` que usa relleno por cara para comparar.

## Qué pretende demostrar

Que `render_silhouette` tiene un caso de uso claro en juegos: props convexos de un solo color, especialmente fondos, rocas, sombras, selección y elementos de lejanía. En esta escena la silueta permite mantener 25 fps con una flota visible, mientras el relleno por cara baja a 17,7 fps. No pretende demostrar que sirva para personajes con materiales.

## Elementos en pantalla

- Ocho rocas naranjas estáticas en la parte superior, dibujadas una vez por buffer.
- Dos naves de color plano (cian/azul) que se mueven y rotan en la parte inferior.
- Fondo negro y HUD con nombre, frames, fps, modo `SIL` y número de objetos pintados.

## Evolución temporal

Las naves se desplazan y giran; las rocas permanecen fijas. La ruta de silueta redibuja solo las dos naves dinámicas; el A/B `K096_FACE=1` redibuja sus caras y baja la cadencia. La secuencia y la visión validan contornos limpios y ausencia de restos.