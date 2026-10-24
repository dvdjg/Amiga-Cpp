# Demo 092 - Personaje articulado (PBD, inercia angular y Blitter)

Tres partes rigidas (torso, cadera y pierna) forman una cadena con pivotes, masa, inercia diagonal real y estado angular. `ArticulatedBody3D` integra traslacion y rotacion, resuelve joints de bola por PBD y limita cadera/pierna con bisagras. El render usa `Scene::render_silhouette` con `PolygonFillService` y Blitter.

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/092_articulated_character --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/092_articulated_character --settle-ms 700 --sequence-frames 4 --sequence-interval-ms 1000
node ./tools/analyze/verify-3d-sequence.mjs out/run/092_articulated_character/A500_debug/sequence --min-frames 3 --require-motion
```

La variante de referencia llena siluetas (2 planos). La variante de coste `EXTRA_DEFINES=-DK092_WIRE=1` usa alambre de 1 plano; sirve para medir P8, no es la ruta visual por defecto. El asset Model3D v2 puede describir estas partes y sus esferas; esta demo las construye in-situ para aislar el solver.

Estado medido (2026-10-24): silhouette release **12,6 fps / 3,97 campos** (562.835 ciclos); debug **12,0 fps / 4,17 campos**. La cadena se asienta y los joints animan cadera/pierna; vision de 4 frames confirma movimiento y contornos limpios. **25 fps siguen abiertos**: el cuello es el coste de jobs de linea/fill por frame, no la fisica (ablacion sin fisica: 15,25 fps wire).