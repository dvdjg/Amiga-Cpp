# 211: teleport de BOBs de un plano

Demo del camino de degradación de `eng::scene::PlaneBobLayer`: tres BOBs de un plano sobre un framebuffer interleaved de 5 planos. Dos de ellos saltan cada 32 frames a una posición nueva, con un desplazamiento mayor que el padding; la capa emite **clear de la huella previa + draw**. El tercero se mueve despacio y solo usa la copia opaca.

La secuencia confirma que no queda el BOB viejo: en el frame del salto el HUD sube de `J3/B3` a `J5/B5` (dos clear extra) y la imagen vuelve a `J3/B3` al frame siguiente. No se usa save-under, sprites hardware ni doble buffer.

## API

```cpp
eng::scene::PlaneBobLayer bobs;
bobs.set_sheet(hoja, 2, 2);
bobs.resize(3);
bobs[i] = {x, y, frame, plane, true};
bobs.emit(plan, target);
```

`bobs.emit(plan, target, group_base)` marcaría cada erase+draw como grupo atómico, pero esta demo no activa el scheduler beam-aware: con el estado actual baja el presupuesto y reaparece el fantasma de la base.

## Medidas

- `A500_release`: **49,91 fps**, 142.144 ciclos/frame, 1,002 campos/frame.
- Secuencia de 40 frames: sin banda inferior, sin BOB viejo y sin cortes; el frame de teleport usa 5 jobs.
- `detail=0x21100000`, canarios intactos.

## Verificación

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/playfield/211_plane_bob_teleport --release
node tools/debug/measure-fps.mjs 211_plane_bob_teleport A500_release
bash tools/run/run-demo.sh demos/techniques/amiga/playfield/211_plane_bob_teleport --config A500_release --sequence-frames 40 --sequence-interval-ms 20
```
