# Validación — 225 SpriteDirector 50 Hz (CPU)

## Build y ejecución

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/sprites/225_sprite_director_stress --release
bash tools/run/run-demo.sh demos/techniques/amiga/sprites/225_sprite_director_stress --config A500_release --warp
node tools/debug/measure-fps.mjs 225_sprite_director_stress A500_release --json
```

## Medida entregada (carga reducida)

```text
225_sprite_director_stress/A500_release | emulado=50.08 fps | host=49.88 fps | 141651 ciclos/frame | 1.00 campo/frame
```

La variante entregada conserva una entidad CPU y el fondo/HUD; cumple 50 Hz PAL.

## Escena completa (rechazada para la demo)

La escena original de estrés (varios sprites HW, par *attached*, tira, compuesto, BOB, CPU y `BandPalette`) midió:

```text
emulado=26.46 fps | host=26.40 fps | 268129 ciclos/frame | 1.89 campos/frame
```

No cumple el criterio de 50 fps y por eso **no se publica como demo válida**. Para llegar a 50 Hz se redujo la carga: sólo CPU, sin banda de paleta, sin tira, sin compuesto, sin par *attached* y sin BOB. La ruta BOB se valida por separado en 126 (49,92 fps) y la contabilidad del sistema en HOST-485/491.

## Visión

La captura de la variante entregada muestra el bloque CPU en el playfield y el HUD de una línea. `qwen3-vl:8b-instruct-q8_0` confirma que la entidad CPU está presente y separada del resto del arte; no se observan recortes ni artefactos.
