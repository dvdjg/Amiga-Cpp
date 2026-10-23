# INTENT - 091_scene3d_nodes

## Qué hace

Demo de la fachada 3D de juego (`eng::scene3d::Scene` + `ModelTable`): dos cubos viven en una `Scene<8, 8>` con handles generacionales. El cubo raíz gira en Y/Z; el cubo hijo cuelga de él con `set_parent` y gira sobre sí mismo con otro multiplicador, de modo que hereda la rotación del padre. El render es en alambre sobre un display de 1 plano; la ruta usa el rasterizador Blitter y ejecuta un `FramePlan` por frame para medir el coste real.

## Qué pretende demostrar

Que el juego describe nodos, transforms, jerarquía y modelos cocinados sin tocar planos ni Copper; que los handles invalidan correctamente al borrar/reutilizar; y que el render 3D usa la misma infraestructura de playfield y HUD que las demás demos. La variante `K091_WIRE=0` sirve como referencia de caras rellenas cuando el presupuesto lo permita.

## Elementos en pantalla

- Fondo de 320x256 en 1 plano; la franja inferior (208..255) contiene el rótulo "091 SCENE3D NODES".
- Dos cubos de distinto tamaño (28 y 16 unidades) dibujados como wireframe blanco en el área 3D.
- HUD de host con título, versión, frame, FPS y estadísticas de aristas/jobs del plan.
- Sin playfield de juego ni sprites hardware: la demo mide la escena 3D retenida.

## Evolución temporal

Cada frame incrementa `m_phase`; el raíz aplica una rotación Y/Z dependiente de la fase y el hijo rota con multiplicadores distintos conservando la herencia del padre. El área 3D se limpia por Blitter antes de dibujar, el wireframe se encola en `m_plan` y se ejecuta, y el HUD actualiza frame/FPS y el número de aristas/jobs. El estado es determinista y no usa heap.
