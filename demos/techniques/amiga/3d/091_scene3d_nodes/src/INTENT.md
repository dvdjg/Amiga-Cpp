# INTENT - 091_scene3d_nodes

## Qué hace

Demo de la fachada 3D de juego (`eng::scene3d::Scene` + `ModelTable`): tres cubos viven en una `Scene<8, 8>` con handles generacionales. El cubo raíz gira en Y/Z; dos cubos hijos cuelgan de él con `set_parent` y giran sobre sí mismos, de modo que heredan la rotación del padre. El render es en alambre por defecto (`K091_WIRE=1`) para aislar la escena y la jerarquía del coste del relleno por Blitter.

## Qué pretende demostrar

Que el juego describe nodos, transforms, jerarquía y modelos cocinados sin tocar planos ni Copper; que los handles invalidan correctamente al borrar/reutilizar; y que el render 3D usa la misma infraestructura de playfield y HUD que las demás demos. La variante `K091_WIRE=0` sirve como referencia de caras rellenas por Blitter cuando el presupuesto lo permita.

## Elementos en pantalla

- Fondo de 320x256 en 4 planos; la franja inferior (208..255) contiene el rótulo "091 SCENE3D NODES".
- Tres cubos de distinto tamaño (28, 14 y 10 unidades) dibujados como wireframe en el área 3D.
- HUD de host con título, versión, frame y FPS en la esquina superior.
- Sin playfield de juego ni sprites hardware: la demo mide la escena 3D retenida.

## Evolución temporal

Cada frame incrementa `m_phase`; el raíz aplica una rotación Y/Z dependiente de la fase y los hijos rotan con multiplicadores distintos (5/7 y 11/13) conservando la herencia del padre. El área 3D se limpia por Blitter antes de dibujar, y el HUD actualiza frame/FPS. El estado es determinista y no usa heap.
