# 093 - Animación rígida con frames visualmente distintos (G4)

Demo de la API de animación rígida: un torso estático con cabeza y un brazo de dos segmentos animado por `eng::anim3d::Player`. El clip tiene dos tracks (hombro y codo), 12 ticks y eventos; la cadena se compone por la jerarquía de `eng::scene3d::Scene`.

El objetivo de esta demo es doble:

- validar el runtime de clips y la interpolación shortest-turns en el 68000;
- medir **frames visualmente distintos**, no vueltas del bucle de simulación. El HUD usa una firma de las aristas proyectadas del brazo y solo incrementa `F` cuando cambia; su `FPS` es, por tanto, el fps visual.

## Técnica

- **Capa estática**: torso, cabeza, piernas, pies, brazo izquierdo y suelo se proyectan una vez y se hornean en el plano 0 de los tres buffers. No se limpian ni se redibujan por frame.
- **Capa dinámica**: el brazo y el antebrazo viven en el plano 1. Cada frame se limpia solo ese plano, se muestrea el clip, se actualizan dos rotaciones y se dibujan sus aristas con `retro::wire_lines`.
- **Color por modelo**: `Scene::collect_wire_edges(..., model_color=true)` usa el color del material de cada modelo; el cuerpo sale en blanco y la cadena en cian.
- **Contador visual**: firma de aristas proyectadas del brazo; si no cambia, `F` no sube.
- **Clip**: hombro `0 → -0.35 → 0` turns y codo `0 → -0.20 → 0` en 12 ticks. El brazo se mueve fuera del torso para que la silueta cambie en cada frame.

## Búsqueda previa

Antes de escribir la demo se comprobó que las piezas existían y se reutilizan:

- `eng::scene3d::Scene::collect_wire_edges` para proyectar aristas sin `FramePlan`.
- `eng::retro::wire_lines` para trazarlas por Blitter.
- `eng::anim3d::Player`, `Clip`, `Track`, `Keyframe` y `Event` (HOST-493).
- `eng::debug::HostHud` para el overlay de nombre/versión/fps.
- `eng::graphics::composition::Scene` y `PolygonFillService` como infraestructura de display.

No se añade ningún renderer paralelo ni una ruta de líneas CPU.

## Build / run

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/093_rigid_anim --debug
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/093_rigid_anim --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/093_rigid_anim
```

## Límites conocidos

- El cuerpo y la cámara son fijos; por eso la capa estática puede hornearse. Un cuerpo móvil requiere recalcular esa capa o moverla a la capa dinámica (G5).
- El contador visual usa un hash muestreado, no compara buffers completos; el gate de secuencia usa hashes exactos del área 3D.
- Solo se animan dos joints rígidos; no hay clips de asset, morph targets ni skinning. Eso queda para G1/G4 posteriores.
