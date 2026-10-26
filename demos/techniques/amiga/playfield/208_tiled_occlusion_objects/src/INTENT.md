# INTENT - 208_tiled_occlusion_objects

## Qué hace

Extiende el soporte Tiled con **objetos de runtime ordenados por profundidad**. La casa de `Beginning Fields` se cuece en la base; un árbol y una farola no se hornean y llegan al Amiga como `MapObject` con `foot_y`:

- id 43: `cook=foreground`, `sort_y=base`; se dibuja entre los actores según su pie.
- id 143: `cook=occluder`; se dibuja después de todos los actores.

## Qué pretende demostrar

- Que el cook-plan puede declarar el rol de cada objeto (`cook`, `sort_y`, `occluder`) por propiedades, reglas de capa o `default`, y que los objetos `baked` se funden en la base mientras los de runtime se emiten aparte.
- Que `MapObjectsView` entrega una tabla ordenada con `foot_y` y permite intercalar props y actores sin que el juego ordene por pantalla.
- Que el arte de un objeto Tiled (árbol 64×63 y farola 46×62) se puede cocinar como planar 6bpp + máscara 1bpp y como hoja par interleaved con guarda para un blit.
- Que una casa cocida, un árbol foreground, una farola occluder y un actor delante/detrás se leen de un vistazo.

## Elementos en pantalla

- Mapa `Beginning Fields` EHB 6 planos, 320×208 visibles + 48 px de HUD negro.
- Casa cocida en la base; árbol id 43 a la izquierda de la casa; farola id 143 como occluder; camino, rocas, flores y vallas del bake.
- Un explorador móvil de 16×24 (dos frames de zancada) que cruza el árbol y la farola; un segundo explorador de guardia, horneado una vez en el anillo, permanece junto al banco. El móvil queda detrás del árbol cuando su `foot_y` es menor y delante cuando es mayor; la farola se pinta siempre encima.
- HUD de host con nombre, versión, frame y fps; la línea de estado muestra `SPLIT`, `A0` y `A1`.

## Evolución temporal

1. **Init**: se carga el `MapBundle`, se eligen los dos `MapObject`, se construyen los `Sprite` de props y las hojas pares de actor, se hornea el explorador de guardia, se rellena el anillo de 288 líneas y se pre-scrollea a la ventana (200, 0).
2. **Frames**: el explorador móvil avanza 1 px por eje; se pinta el prop cuyo `sort_key` es menor que su pie, luego el actor (con save-under), luego el resto de props y al final el occluder. El guardia ya forma parte del fondo.
3. **Cierre**: bucle continuo por la ruta de 10 puntos; no hay scroll de cámara.
