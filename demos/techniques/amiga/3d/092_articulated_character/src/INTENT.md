# Intencion de la demo 092

## Qué hace

Construye una cadena de tres partes rigidas (torso, cadera y pierna) con masa, pivotes y una esfera de colision por parte. `ArticulatedBody3D` integra gravedad y resuelve joints de bola por PBD. El resultado se dibuja como siluetas con `PolygonFillService` y Blitter.

## Qué pretende demostrar

Que el formato Model3D v2 puede describir partes, esferas, masa, densidad, friccion, restitucion y AABB; que el runtime puede agrupar esas esferas en cuerpos con masa/inercia; y que una cadena articulada se mantiene unida con un coste aceptable en A500. Tambien comprueba que el render por silueta con Blitter no deja huecos ni artefactos.

## Elementos en pantalla

- Tres rectangulos solidos de colores (torso, cadera y pierna) unidos por pivotes.
- Fondo negro uniforme.
- HUD con nombre de demo, version, frame, fps y conteos `P3 S3 F3`.

## Evolución temporal

La cadena parte de una pose doblada, cae por gravedad y se asienta; los joints conservan los pivotes y las esferas siguen a cada parte. La secuencia valida la estabilidad de la pose y la ausencia de huecos; no es un ciclo de animacion continuo. El siguiente corte anade joints angulares, inercia angular y un ciclo de marcha.