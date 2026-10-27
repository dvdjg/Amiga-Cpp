# INTENT - 208_tiled_occlusion_objects

## Qué hace

Extiende el soporte Tiled con **objetos de runtime ordenados por profundidad**. La casa de `Beginning Fields` se cuece en la base; un árbol, una farola y un cartel no se hornean y llegan al Amiga como `MapObject` con `foot_y`:

- id 43: `cook=foreground`, `sort_y=base`; se dibuja entre los actores según su pie.
- id 143: `cook=occluder`; se dibuja después de todos los actores.
- id 89: `cook=occluder`; cartel de madera que queda delante de los actores con pie menor, pero que el explorador móvil cruza por delante en esta ruta.

## Qué pretende demostrar

- Que el cook-plan puede declarar el rol de cada objeto (`cook`, `sort_y`, `occluder`) por propiedades, reglas de capa o `default`, y que los objetos `baked` se funden en la base mientras los de runtime se emiten aparte.
- Que `MapObjectsView` entrega una tabla ordenada con `foot_y` y permite intercalar props y actores sin que el juego ordene por pantalla.
- Que el arte de un objeto Tiled (árbol 64×63, farola 48×62, cartel 48×42) se puede cocinar como planar 6bpp + máscara 1bpp y como hoja par interleaved con guarda para un blit.
- Que el orden por pie del actor frente a props y occluders se puede validar con secuencia y visión por entidades.

## Elementos en pantalla

- Mapa `Beginning Fields` EHB 6 planos, 320×208 visibles + 48 px de HUD negro, cámara fija en `X=200`, `Y=50`.
- Casa cocida en la base; árbol id 43 a la izquierda de la casa; farola id 143 como occluder; cartel id 89 como occluder; camino, rocas, flores y vallas del bake.
- Un explorador móvil de 16×24 (dos frames de zancada) que pasa por delante del cartel, cruza detrás del árbol y pasa por detrás del guardia; el explorador de guardia se pinta una vez en el anillo y se repinta como overlay durante el cruce. El móvil queda detrás del árbol cuando su `foot_y` es menor y delante cuando es mayor; no se oculta de golpe.
- HUD de host con nombre, versión, frame y fps; la línea de estado muestra `SPLIT`, `A0`, `A1`, `X1` e `Y1`.

## Evolución temporal

1. **Init**: se carga el `MapBundle`, se eligen los tres `MapObject`, se construyen los `Sprite` de props y las hojas pares de actor, se hornea el explorador de guardia, se rellena el anillo de 288 líneas y se pre-scrollea a la ventana (200, 50).
2. **Frames**: el explorador móvil avanza 1 px por eje; se pinta el prop cuyo `sort_key` es menor que su pie, luego el actor (con save-under), luego el resto de props, el overlay del guardia cuando corresponde y al final los occluders. El cartel está en la base.
3. **Cierre**: bucle continuo por la ruta de 13 puntos; no hay scroll de cámara. El frame ocupa 2 campos (25,0 fps).
