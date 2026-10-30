# 210: BOBs de bitmap sobre 5 planos

Demo de la capa de alto nivel `eng::scene::PlaneBobLayer`. Una hoja de 12×12 (arte 8×8 + padding de 2 px), un plano por actor y un framebuffer interleaved de 5 planos. Cada BOB se dibuja con **un solo blit opaco** (`D=A`).

## API

```cpp
eng::scene::PlaneBobLayer bobs;
bobs.set_sheet(hoja, 2, 2);       // padding por lado
bobs.resize(6);
bobs[0] = {x, y, frame, plane, true};
bobs.emit(plan, target);         // la capa gestiona la huella previa
```

La capa:

- usa el padding para limpiar el movimiento pequeño (un job por BOB);
- si el actor salta más que el padding, emite **clear de la huella previa + draw**;
- puede marcar cada actor como grupo atómico con `emit(plan, target, 0)`;
- no usa heap ni conoce minterns/módulos.

## Estado medido

| BOBs | fps | Secuencia |
|---:|---:|---|
| 6 | **49,9** | limpia en 30 frames |
| 8 | **42,5** | fuera de 50; solo referencia |

`kBobCount` es configurable en compilación:

```bash
EXTRA_DEFINES="-DK_BOB_COUNT=8" bash tools/build/build-demo.sh \
  demos/techniques/amiga/playfield/210_beam_aware_bobs --release
```

El valor por defecto es 6. La agrupación beam-aware del backend está disponible en la API, pero **no se usa en este demo**: con el scheduler actual baja a 42,7 fps (6) / 33,3 (8) y aparece el fantasma de la base; queda como línea de trabajo separada, no como camino aceptado.

## Verificación

```bash
node tools/debug/measure-fps.mjs 210_beam_aware_bobs A500_release
bash tools/run-host-tests.sh tests/host/graphics/505_plane_bob_layer
bash tools/check/render-gates.sh
```
