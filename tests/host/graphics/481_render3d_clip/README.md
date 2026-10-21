# HOST-481 — Render 3D y clipping

Verifica `eng::graphics::render3d` con `float` y `Fixed<s16,0>`: recorte de cámara contra near y los cuatro laterales, proyección acotada, winding/culling, pentágonos n-gon y el comportamiento de `cap` del buffer de salida. Comprueba además que una arista fija compartida produce el mismo punto de intersección con ambos sentidos de winding, que una cara que cruza near desactiva el fast path aunque el yaw sea moderado, y que el culling de las caras del cubo coincide con el oráculo geométrico en yaw ± y giro compuesto.

Incluye un cubo convexo estático en el centro, cruzando por separado los cuatro bordes, cruzando las cuatro esquinas y totalmente fuera por la derecha. Para cada postura visible, las caras que recibe `Surface` deben tener al menos tres vértices y todas las coordenadas dentro de `[0,320) × [0,200)`; el caso exterior debe emitir cero caras. Un pentágono se escala y traslada hasta cruzar cada borde, y un tetraedro irregular cruza el lateral derecho; ambos se rasterizan con la referencia CPU por plano y se comprueba la cobertura.

El caso de capacidad reproduce el fallo si el recortador escribe más vértices que los declarados por `cap`: debe devolver cero sin modificar la memoria que queda fuera de la vista.

Ejecutar desde la raíz del repositorio:

```bash
bash tools/run-host-tests.sh tests/host/graphics/481_render3d_clip
```
