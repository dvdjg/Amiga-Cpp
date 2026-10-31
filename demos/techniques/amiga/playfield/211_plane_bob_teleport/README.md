# 211: BOBs de un plano a velocidad alta

Demo del camino de degradación de `eng::scene::PlaneBobLayer`: tres BOBs de un plano sobre un framebuffer interleaved de 5 planos. Los tres avanzan **1 px/frame con padding 0**, así que la capa emite **clear de la huella previa + draw** en cada frame. Las trayectorias son continuas, con rebote en los bordes; no hay teleports.

La secuencia confirma que no queda el BOB viejo: cada frame usa `J6/B6` (tres clear + tres draw). No se usa save-under, sprites hardware ni doble buffer.

## API

```cpp
eng::scene::PlaneBobLayer bobs;
bobs.set_sheet(hoja, 0, 0);
bobs.resize(3);
bobs[i] = {x, y, frame, plane, true};
bobs.emit(plan, target);
```

`bobs.emit(plan, target, group_base)` marcaría cada erase+draw como grupo atómico, pero esta demo no activa el scheduler beam-aware: con el estado actual baja el presupuesto y reaparece el fantasma de la base.

## Medidas

- `A500_release`: **49,94 fps**, 142.049 ciclos/frame, 1,001 campos/frame.
- Secuencia de 30 frames: sin banda inferior, sin BOB viejo, sin cortes y sin saltos de posición.
- `detail=0x21100000`, canarios intactos.

## Verificación

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/playfield/211_plane_bob_teleport --release
node tools/debug/measure-fps.mjs 211_plane_bob_teleport A500_release
bash tools/run/run-demo.sh demos/techniques/amiga/playfield/211_plane_bob_teleport --config A500_release --sequence-frames 40 --sequence-interval-ms 20
```
