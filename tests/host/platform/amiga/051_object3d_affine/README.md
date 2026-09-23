# HOST-051 — bit-exactitud de `update_object_transformation` (red para migrar `object3d`)

Segunda red de seguridad de F3: la transformación del objeto construida con la
librería genérica (`Affine<3, q12, q0>`) debe dar **exactamente** los mismos valores
que `object3d::update_object_transformation` actual, para poder cambiar la
implementación sin riesgo.

## Qué valida

- `objectToWorld`: parte lineal (`q12`) + traslación (`q0`, la LONGITUD) — es decir, el
  **split ratio/longitud** que el tipado hace explícito.
- `worldToObject`: el compuesto `escala⁻¹ · rotación⁻¹`.
- **La cámara**: `normfx(M · t)` — una **LONGITUD** (exp 0), no un 4.12.
- Barrido de los 4096 ángulos (paso 7) con la configuración de la demo
  (`scale = 1.0`, `translate = (0,0,−4000)`), comparando los 12 valores y la cámara.
- `Object3D::rotate` va en **radianes** (`Angle3` = `q12`); el test construye el ángulo con
  `angle_to_radians` y el espejo con la tabla directa, así que la exactitud se fija contra
  la tabla del original.

## Por qué importa

`objectToWorld`/`worldToObject` llevan la **misma longitud** que antes (24 bytes), pero
distribuida como lineal (9) + traslación (3) en vez de intercalada. Con esto se puede
cambiar el tipo sin que cambie ni un valor; el único punto a revisar es el asm de la
demo, que indexa esos 24 bytes por offset fijo.

## Ejecutar

```
CXX="C:\Users\dvdjg\Documents\programa\AI\Amiga\mingw64\bin\g++.exe" \
  bash tools/run-host-tests.sh tests/host/platform/amiga/051_object3d_affine
```
