# HOST-053 — tabla dorada de la proyección (`lib3d::transform_vertices`)

A ángulo **fijo**, fija los valores proyectados y la bounding-box para que ningún
cambio en la capa de cálculo (matrices, exponentes, redondeo, `div_wide`) los altere en
silencio. Es el trinquete determinista que complementa a `HOST-047`.

## Qué valida

Malla mínima (3 nodos en `(100,0,0)`, `(0,100,0)`, `(0,0,100)` + 1 arista + 1 cara),
rotación **1000** en los tres ejes y traslación `(0,0,-4000)`:

- **Golden**: cámara `(3988,291,4)`; vértices `(128,128,−3901)`, `(128,134,−3993)`,
  `(122,128,−4000)`; bbox `(122,128,128,134)`.
- **Cordura geométrica** (para que la tabla no sea "dorada basura"): cada vértice cae
  dentro del viewport, su `zp` es negativo (delante de la cámara), la bbox está ordenada
  y coincide con los extremos de los vértices proyectados.

## Por qué a ángulo fijo

La proyección depende del ángulo; con uno variable el resultado no es comparable entre
ejecuciones. Fijarlo hace el test reproducible y, junto con el gate de fase congelada de
la demo, permite afirmar "esto no cambia la imagen" con evidencia.

## Ejecutar

```
CXX="C:\Users\dvdjg\Documents\programa\AI\Amiga\mingw64\bin\g++.exe" \
  bash tools/run-host-tests.sh tests/host/graphics/053_lib3d_projection
```
