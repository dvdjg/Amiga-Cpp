# 095 - Collision 3D (trigger y capas)

Demo de G6: trigger OBB + filtro de capas + consulta esfera-OBB, con render de siluetas por Blitter. El cubo amarillo recorre la puerta; el HUD muestra `IN/OUT`, la capa y el numero de objetos pintados.

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/095_collision3d --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/095_collision3d --config A500_release
```

La validacion visual esta en [`src/VISION.md`](src/VISION.md) y la intencion en [`src/INTENT.md`](src/INTENT.md).