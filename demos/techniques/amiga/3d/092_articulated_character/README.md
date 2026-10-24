# Demo 092 - Personaje articulado (PBD, esferas y Blitter)

Tres partes rigidas (torso, cadera y pierna) forman una cadena articulada con pivotes y masa. `ArticulatedBody3D` integra gravedad y resuelve joints de bola por PBD; cada parte aporta una esfera de colision y masa/inercia. El render usa `Scene::render_silhouette` con `PolygonFillService` y el Blitter, no CPU.

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/092_articulated_character --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/092_articulated_character --settle-ms 1500 --sequence-frames 4 --sequence-interval-ms 300
```

El asset Model3D v2 puede describir estas partes/esferas (`MODEL3D_ASSET_FORMAT.md`); esta demo las construye in-situ para aislar el solver.

Estado medido: 13,60 fps / 3,68 campos (521.902 ciclos), 2 planos de mundo con fill Blitter; la vision confirma tres partes con contornos limpios y erify-3d-sequence.mjs valida la secuencia. La cadena PBD queda estatica tras asentarse; el siguiente corte anade joints angulares y un ciclo de animacion.
